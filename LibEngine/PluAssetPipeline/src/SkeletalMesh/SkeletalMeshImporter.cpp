//
// Created by Plutex on 7/5/26.
//

#include "PluEngine/AssetPipeline/SkeletalMesh/SkeletalMeshImporter.h"

#include <assimp/Importer.hpp>
#include <assimp/postprocess.h>
#include <assimp/scene.h>
#include <assimp/config.h>

#include <cmath>
#include <cstring>
#include "glm/gtc/matrix_transform.hpp"
#include "glm/gtc/quaternion.hpp"
#include "glm/gtc/type_ptr.hpp"

#include "PluEngine/PluPaths.h"
#include "PluEngine/AssetPipeline/SkeletalMesh/SkeletalMeshAssetLoader.h"
#include "PluEngine/AssetPipeline/Mesh/MeshProcessing.h"
#include "PluEngine/AssetTypes/Skeleton/Skeleton.h"
#include "PluEngine/AssetTypes/SkeletalMesh/SkeletalMesh.h"
#include "PluEngine/AssetCore/AssetDescriptor.h"
#include "PluEngine/AssetTypes/Animation/SkeletalAnimation.h"
#include "PluEngine/Core/DiskManager.h"

namespace Plu
{
    using MeshProcessing::AssimpToGLM;

    // void ImportSkeletons(aiScene* scene, TUsePointer<EngineAssetManager> assetManager, DynamicArray<Skeleton>* skeletons)
    // {
    //     std::function<SkeletonBone(aiSkeletonBone*)> makeBoneFromAssimp = [](aiSkeletonBone* skeletonBone) -> SkeletonBone {
    //         SkeletonBone bone;
    //         bone.NodeName = skeletonBone->mNode->mName.C_Str();
    //         bone.LocalMatrix = AssimpToGLM(skeletonBone->mLocalMatrix);
    //         bone.OffsetMatrix = AssimpToGLM(skeletonBone->mOffsetMatrix);
    //         return bone;
    //     };
    //
    //     for (int i = 0; i < scene->mNumSkeletons; ++i) {
    //         aiSkeleton* skeletonAssimp = scene->mSkeletons[i];
    //
    //         Skeleton skeleton;
    //         skeleton.SkeletonName = skeletonAssimp->mName.C_Str();
    //
    //         HashMap<int, DynamicArray<unsigned int>> childrenMap;
    //
    //         std::function<void(int, SkeletonBone*)> makeBoneHierarchy = [&](int boneIndex, SkeletonBone* parent) {
    //             SkeletonBone bone = makeBoneFromAssimp(skeletonAssimp->mBones[boneIndex]);
    //             parent->Children.PushBack(bone);
    //             if (!childrenMap.Contains(boneIndex)) return;
    //             for (int child : *childrenMap.Find(boneIndex)) {
    //                 makeBoneHierarchy(child, &bone);
    //             }
    //         };
    //
    //         for (int j = 0; j < skeletonAssimp->mNumBones; ++j) {
    //             aiSkeletonBone* bone = skeletonAssimp->mBones[j];
    //             childrenMap[skeletonAssimp->mBones[i]->mParent].PushBack(i);
    //             if (bone->mParent == -1) {
    //                 skeleton.RootNode = makeBoneFromAssimp(bone);
    //             }
    //         }
    //
    //         for (int idx : childrenMap[-1]) {
    //             makeBoneHierarchy(idx, &skeleton.RootBone);
    //         }
    //         skeletons->PushBack(skeleton);
    //     }
    //     PLU_CORE_WARN("Skeletons imported with new Assimp API is {}", skeletons->Size());
    // } TODO wait for future

    namespace
    {
        // Reads one integer scene-metadata entry, returning `fallback` when the file has none.
        int SceneMetaInt(const aiScene* scene, const char* key, int fallback)
        {
            int value = 0;
            if (scene->mMetaData && scene->mMetaData->Get(key, value)) return value;
            return fallback;
        }

        // The axis conversion Assimp says it performed on this file, as a rotation.
        //
        // Assimp records the file's original up axis alongside the one it converted to. When a
        // file arrives half-converted (see SkeletalMeshImportOptions::RebuildBindPoseFromSkinning)
        // the skinning matrices are still in the *original* space, so putting a rebuilt bind pose
        // back into the scene's space needs exactly this rotation. Identity when the file declares
        // no conversion (OriginalUpAxis is -1 for files Assimp left alone) or none was needed.
        Matrix4 DeclaredAxisConversion(const aiScene* scene)
        {
            const int originalUp = SceneMetaInt(scene, "OriginalUpAxis", -1);
            if (originalUp < 0 || originalUp > 2) return Matrix4(1.0f);

            const int targetUp = SceneMetaInt(scene, "UpAxis", 1);
            if (targetUp < 0 || targetUp > 2) return Matrix4(1.0f);

            const int originalSign = SceneMetaInt(scene, "OriginalUpAxisSign", 1) < 0 ? -1 : 1;
            const int targetSign = SceneMetaInt(scene, "UpAxisSign", 1) < 0 ? -1 : 1;
            if (originalUp == targetUp && originalSign == targetSign) return Matrix4(1.0f);

            Vec3 from(0.0f), to(0.0f);
            from[originalUp] = static_cast<float>(originalSign);
            to[targetUp] = static_cast<float>(targetSign);

            const float cosAngle = glm::clamp(glm::dot(from, to), -1.0f, 1.0f);
            Vec3 axis = glm::cross(from, to);
            if (glm::length(axis) < 1e-6f)
            {
                // Parallel (nothing to do) or antiparallel — for the latter any perpendicular axis
                // gives a valid 180° turn, so pick one that isn't degenerate.
                if (cosAngle > 0.0f) return Matrix4(1.0f);
                axis = std::fabs(from.x) < 0.9f ? Vec3(1.0f, 0.0f, 0.0f) : Vec3(0.0f, 1.0f, 0.0f);
                axis = glm::normalize(glm::cross(from, axis));
            }
            else
            {
                axis = glm::normalize(axis);
            }

            return glm::rotate(Matrix4(1.0f), std::acos(cosAngle), axis);
        }

        // Walks up an Assimp node chain accumulating the rest-pose global transform.
        Matrix4 GlobalFromNodeTree(const aiNode* node)
        {
            Matrix4 global(1.0f);
            for (const aiNode* n = node; n; n = n->mParent)
                global = AssimpToGLM(n->mTransformation) * global;
            return global;
        }

        // Does the rest pose stored in the node hierarchy actually agree with the inverse-bind
        // matrices the mesh is skinned with? For a healthy file the two are inverses of each other
        // for every bone, so any real disagreement means the hierarchy cannot be trusted as a bind
        // pose. Tolerance scales with the rig's own size — these are model units, not metres.
        bool BindPoseAgreesWithSkinning(const HashMap<String, aiBone*>& bones, float* outWorstDelta)
        {
            float rigExtent = 0.0f;
            float worst = 0.0f;

            for (const auto& entry : bones)
            {
                const aiBone* bone = entry.second;
                if (!bone->mNode) continue;

                const Matrix4 fromSkinning = glm::inverse(AssimpToGLM(bone->mOffsetMatrix));
                const Matrix4 fromNodes = GlobalFromNodeTree(bone->mNode);

                rigExtent = glm::max(rigExtent, glm::length(Vec3(fromSkinning[3])));

                const float* a = glm::value_ptr(fromNodes);
                const float* b = glm::value_ptr(fromSkinning);
                for (int i = 0; i < 16; ++i)
                    worst = glm::max(worst, std::fabs(a[i] - b[i]));
            }

            if (outWorstDelta) *outWorstDelta = worst;
            // 1% of the rig's reach: comfortably above float noise in the matrix chain, far below
            // the double-digit displacements a mis-converted rig produces.
            return worst <= glm::max(1e-4f, 0.01f * rigExtent);
        }

        // Rewrites every node's LocalMatrix so the skeleton's bind pose matches what the mesh was
        // skinned against: a bone's global transform is the inverse of its offset matrix, by
        // definition of the offset. Nodes that skin nothing have no such reference and keep the
        // local transform the file gave them, which preserves their placement relative to the
        // parent above them.
        //
        // `axisConversion` puts the result back into the space the rest of the scene (and its
        // animations) live in — the offsets are in the file's pre-conversion space.
        void RebuildBindPose(SkeletonNode* node, const HashMap<String, aiBone*>& bones,
                             const Matrix4& axisConversion, const Matrix4& parentGlobal)
        {
            if (!node) return;

            Matrix4 global;
            if (aiBone* const* bone = bones.Find(node->NodeName))
                global = axisConversion * glm::inverse(AssimpToGLM((*bone)->mOffsetMatrix));
            else
                global = parentGlobal * node->LocalMatrix;

            node->LocalMatrix = glm::inverse(parentGlobal) * global;

            for (const TOwningPointer<SkeletonNode>& child : node->Children)
                RebuildBindPose(child.GetRaw(), bones, axisConversion, global);
        }
    }

    void ImportSkeletonsLegacy(aiScene* scene, TUsePointer<EngineAssetManager> assetManager,
                               DynamicArray<Skeleton>* skeletons, bool rebuildBindPoseFromSkinning)
    {
        for (int i = 0; i < scene->mNumMeshes; ++i) {
            aiMesh* mesh = scene->mMeshes[i];
            if (!mesh->HasBones()) continue;
            aiNode* root;
            std::function<void(aiNode*)> findRoot = [&](aiNode* bone) {
                if (bone->mParent->mName == aiString("RootNode")) {
                    root = bone;
                } else {
                    findRoot(bone->mParent);
                }
            };
            HashMap<String, aiBone*> bones;
            findRoot(mesh->mBones[0]->mNode);
            for (int j = 0; j < mesh->mNumBones; ++j) {
                aiBone* bone = mesh->mBones[j];
                aiNode* rootBefore = root;
                findRoot(bone->mNode);
                if (root != rootBefore) {
                    PLU_CORE_ERROR("Meshes with multiple root bones are not supported!");
                }
                bones.Insert(bone->mNode->mName.C_Str(), bone);
            }

            std::function<TOwningPointer<SkeletonNode>(aiNode*)> constructSkeleton = [&](aiNode* node) {
                bool isBone = bones.Contains(node->mName.C_Str());
                TOwningPointer<SkeletonNode> skeletonNode;
                if (isBone) {
                    TOwningPointer<SkeletonBone> bone = CreateOwning<SkeletonBone>();
                    bone->OffsetMatrix = AssimpToGLM(bones[node->mName.C_Str()]->mOffsetMatrix);
                    skeletonNode = bone;
                } else {
                    skeletonNode = CreateOwning<SkeletonNode>();
                }
                skeletonNode->NodeName = node->mName.C_Str();
                skeletonNode->LocalMatrix = AssimpToGLM(node->mTransformation);
                for (int i = 0; i < node->mNumChildren; ++i) {
                    skeletonNode->Children.PushBack(constructSkeleton(node->mChildren[i]));
                }
                return skeletonNode;
            };
            TOwningPointer<SkeletonNode> rootSkeletonNode = constructSkeleton(root);

            // The hierarchy is only a trustworthy bind pose if it agrees with the skinning
            // matrices; when it doesn't, those matrices are the ones the mesh was authored
            // against, so they win.
            float worstDelta = 0.0f;
            if (!BindPoseAgreesWithSkinning(bones, &worstDelta))
            {
                if (rebuildBindPoseFromSkinning)
                {
                    PLU_CORE_WARN("Mesh '{}': node hierarchy disagrees with its skinning matrices "
                                  "(worst delta {:.3f} model units) — rebuilding the bind pose from "
                                  "the skinning data. This file's rest pose would otherwise be broken",
                                  mesh->mName.C_Str(), worstDelta);
                    RebuildBindPose(rootSkeletonNode.GetRaw(), bones, DeclaredAxisConversion(scene), Matrix4(1.0f));
                }
                else
                {
                    PLU_CORE_WARN("Mesh '{}': node hierarchy disagrees with its skinning matrices "
                                  "(worst delta {:.3f} model units) — importing it as-is, so the rest "
                                  "pose will likely be wrong (RebuildBindPoseFromSkinning is off)",
                                  mesh->mName.C_Str(), worstDelta);
                }
            }

            Skeleton skeleton;
            skeleton.SkeletonName = String(mesh->mName.C_Str()) + "_Skeleton";
            skeleton.RootNode = rootSkeletonNode;
            skeletons->PushBack(skeleton);
        }
    }

    namespace
    {
        // Assigns each SkeletonBone a palette index in DFS pre-order over the skeleton tree.
        // Vertex skinning references bones by this index; the ordering is deterministic and
        // matches a DFS over the (order-preserving) serialized tree, so it is stable on load.
        void BuildBonePalette(const SkeletonNode* node, HashMap<String, UInt32>& outMap)
        {
            if (!node) return;
            if (dynamic_cast<const SkeletonBone*>(node) != nullptr)
                outMap.Insert(node->NodeName, static_cast<UInt32>(outMap.Size()));
            for (const TOwningPointer<SkeletonNode>& child : node->Children)
                BuildBonePalette(child.GetRaw(), outMap);
        }

        // Places (boneIndex, weight) into a free slot of the vertex, or evicts the current
        // smallest influence when all slots are taken and this weight is larger — so each
        // vertex keeps its kMaxBoneInfluence strongest bones.
        void AddBoneInfluence(SkeletalVertex& v, UInt32 boneIndex, float weight)
        {
            if (weight <= 0.0f) return;

            for (int i = 0; i < kMaxBoneInfluence; ++i)
            {
                if (v.BoneWeights[i] == 0.0f)
                {
                    v.BoneIndices[i] = boneIndex;
                    v.BoneWeights[i] = weight;
                    return;
                }
            }

            int minSlot = 0;
            for (int i = 1; i < kMaxBoneInfluence; ++i)
                if (v.BoneWeights[i] < v.BoneWeights[minSlot]) minSlot = i;

            if (weight > v.BoneWeights[minSlot])
            {
                v.BoneIndices[minSlot] = boneIndex;
                v.BoneWeights[minSlot] = weight;
            }
        }

        // Renormalizes each vertex's influences to sum to 1. Vertices with no influence
        // (e.g. unskinned geometry in a skinned mesh) are bound rigidly to bone 0 so they
        // don't collapse to the origin once a skinning shader consumes them.
        void NormalizeBoneWeights(SkeletalMeshData& data)
        {
            for (SkeletalVertex& v : data.Vertices)
            {
                float sum = 0.0f;
                for (int i = 0; i < kMaxBoneInfluence; ++i) sum += v.BoneWeights[i];

                if (sum <= 0.0f)
                {
                    v.BoneIndices[0] = 0;
                    v.BoneWeights[0] = 1.0f;
                    continue;
                }
                for (int i = 0; i < kMaxBoneInfluence; ++i) v.BoneWeights[i] /= sum;
            }
        }

        // Finds the imported skeleton whose bone palette contains every bone the mesh skins
        // to. Returns nullptr if no candidate covers the mesh. On success fills outPalette.
        TUsePointer<Skeleton> FindSkeletonForMesh(aiMesh* mesh,
                                                  const DynamicArray<TUsePointer<Skeleton>>& skeletons,
                                                  HashMap<String, UInt32>& outPalette)
        {
            for (const TUsePointer<Skeleton>& skeleton : skeletons)
            {
                if (!skeleton) continue;

                HashMap<String, UInt32> palette;
                BuildBonePalette(skeleton->RootNode.GetRaw(), palette);

                bool coversAll = true;
                for (UInt32 b = 0; b < mesh->mNumBones; ++b)
                {
                    if (!palette.Contains(mesh->mBones[b]->mName.C_Str())) { coversAll = false; break; }
                }
                if (coversAll)
                {
                    outPalette = palette;
                    return skeleton;
                }
            }
            return {};
        }

        // Collects the names of *all* nodes (bones and plain nodes alike) in the skeleton.
        // Animation channels target nodes by name and often address non-bone nodes (pivots,
        // exporter helper nodes), so a bone-only palette is not enough to match them.
        void CollectNodeNames(const SkeletonNode* node, HashSet<String>& outNames)
        {
            if (!node) return;
            outNames.Insert(node->NodeName);
            for (const TOwningPointer<SkeletonNode>& child : node->Children)
                CollectNodeNames(child.GetRaw(), outNames);
        }

        // Picks the skeleton an animation belongs to by name coverage: how many of the
        // animation's channel target-nodes exist in the skeleton's node set. Returns the
        // best-covering skeleton (highest ratio) or null if nothing matches. Unlike mesh
        // matching we don't require full coverage — animations routinely carry channels for
        // nodes outside the skeleton (cameras, $AssimpFbx$ split nodes). outCoverage, when
        // provided, receives the winning ratio in [0,1].
        TUsePointer<Skeleton> FindSkeletonForAnimation(const aiAnimation* animation,
                                                       const DynamicArray<TUsePointer<Skeleton>>& skeletons,
                                                       float* outCoverage = nullptr)
        {
            TUsePointer<Skeleton> best;
            float bestCoverage = 0.0f;

            for (const TUsePointer<Skeleton>& skeleton : skeletons)
            {
                if (!skeleton) continue;

                HashSet<String> names;
                CollectNodeNames(skeleton->RootNode.GetRaw(), names);

                UInt32 matched = 0;
                for (UInt32 c = 0; c < animation->mNumChannels; ++c)
                    if (names.Contains(animation->mChannels[c]->mNodeName.C_Str()))
                        ++matched;

                const float coverage = animation->mNumChannels > 0
                    ? static_cast<float>(matched) / static_cast<float>(animation->mNumChannels)
                    : 0.0f;

                if (coverage > bestCoverage)
                {
                    bestCoverage = coverage;
                    best = skeleton;
                }
            }

            if (outCoverage) *outCoverage = bestCoverage;
            return best;
        }

        // Flattens the skeleton's node hierarchy into a DFS pre-order list (bones and plain
        // nodes alike). Used by the LocalMatrix animation-channel fallback below.
        void CollectNodes(const TUsePointer<SkeletonNode>& node,
                          DynamicArray<TUsePointer<SkeletonNode>>& out)
        {
            if (!node) return;
            out.PushBack(node);
            for (const TOwningPointer<SkeletonNode>& child : node->Children)
                CollectNodes(child, out);
        }

        // Composes a channel's bind-pose local transform from the first key of each animated
        // component (identity for any component the channel doesn't carry). This is the key we
        // compare against skeleton node LocalMatrix when the channel's exact name is missing.
        // importScale must be the same scale the skeleton was baked with, or the comparison
        // runs in a different space and never matches.
        Matrix4 ComposeChannelBindLocal(const aiNodeAnim* channel, float importScale)
        {
            Vec3 pos(0.0f);
            Quaternion rot(1.0f, 0.0f, 0.0f, 0.0f); // (w, x, y, z) identity
            Vec3 scale(1.0f);
            if (channel->mNumPositionKeys > 0) pos   = AssimpToGLM(channel->mPositionKeys[0].mValue) * importScale;
            if (channel->mNumRotationKeys > 0) rot   = AssimpToGLM(channel->mRotationKeys[0].mValue);
            if (channel->mNumScalingKeys  > 0) scale = AssimpToGLM(channel->mScalingKeys[0].mValue);
            return glm::translate(Matrix4(1.0f), pos)
                 * glm::mat4_cast(rot)
                 * glm::scale(Matrix4(1.0f), scale);
        }

        // Component-wise matrix comparison within an absolute epsilon.
        bool LocalMatricesMatch(const Matrix4& a, const Matrix4& b, float eps = 1e-4f)
        {
            const float* pa = glm::value_ptr(a);
            const float* pb = glm::value_ptr(b);
            for (int i = 0; i < 16; ++i)
                if (std::fabs(pa[i] - pb[i]) > eps) return false;
            return true;
        }

        // Fallback node lookup for animation channels whose exact name isn't in the skeleton.
        // Assimp's FBX pivot splitting can leave the skeleton with a `..._$AssimpFbx$_PreRotation`
        // node while the animation only carries a `..._$AssimpFbx$_Rotation` channel; the two
        // often share the same local matrix. Returns the single unclaimed skeleton node whose
        // LocalMatrix equals `local` (within epsilon), or null when there is no match or the
        // match is ambiguous (more than one candidate).
        TUsePointer<SkeletonNode> FindNodeByLocalMatrix(const Skeleton& skeleton,
                                                        const Matrix4& local,
                                                        const HashSet<String>& claimed)
        {
            DynamicArray<TUsePointer<SkeletonNode>> nodes;
            CollectNodes(skeleton.RootNode, nodes);

            TUsePointer<SkeletonNode> match;
            UInt32 matchCount = 0;
            for (const TUsePointer<SkeletonNode>& node : nodes)
            {
                if (!node || claimed.Contains(node->NodeName)) continue;
                if (!LocalMatricesMatch(node->LocalMatrix, local)) continue;
                ++matchCount;
                if (!match) match = node;
            }

            if (matchCount > 1)
            {
                PLU_CORE_WARN("LocalMatrix fallback ambiguous: {} skeleton nodes share the channel's "
                              "local matrix — leaving channel unmatched", matchCount);
                return nullptr;
            }
            return match;
        }

        // Bakes the import scale into a skeleton in place. Mesh vertices leave
        // ProcessMeshGeometry already multiplied by options.Scale, so the rig has to live in
        // the same space: with an unscaled rig the bind pose still looks correct (global *
        // offset cancels out) but every animated translation displaces bones by unscaled
        // distances and the mesh tears apart — and bone debug draw is the wrong size.
        //
        // Scaling a rig by S means conjugating every matrix: M' = S * M * S^-1. For a uniform S
        // that leaves rotation/scale untouched and only multiplies the translation column, and
        // it composes — (S*A*S^-1) * (S*B*S^-1) = S*(A*B)*S^-1 — so applying it per node keeps
        // the accumulated global transforms correct. The same conjugation on a bone's
        // inverse-bind OffsetMatrix keeps global * offset consistent with the scaled vertices.
        void ScaleSkeletonInPlace(SkeletonNode* node, float scale)
        {
            if (!node) return;

            node->LocalMatrix[3] = Vec4(Vec3(node->LocalMatrix[3]) * scale, node->LocalMatrix[3].w);
            if (SkeletonBone* bone = dynamic_cast<SkeletonBone*>(node))
                bone->OffsetMatrix[3] = Vec4(Vec3(bone->OffsetMatrix[3]) * scale, bone->OffsetMatrix[3].w);

            for (const TOwningPointer<SkeletonNode>& child : node->Children)
                ScaleSkeletonInPlace(child.GetRaw(), scale);
        }

        // Resolves the scale this mesh/clip is actually baked at. Two different quantities meet
        // here: options.Scale converts *this source file's* units to engine units, while
        // Skeleton::ImportScale is the space the rig already lives in. They agree whenever
        // everything comes out of one export, and then this is a no-op.
        //
        // They diverge in two cases that look identical from here, which is why the caller picks
        // via options.UseSkeletonImportScale: a mistyped Scale (rig wins — the default, a mesh
        // must never end up in a different space than its skeleton), or a source file genuinely
        // authored in other units than the rig's file (the typed Scale wins, it is the only
        // thing that knows this file's units).
        float ResolveImportScale(const TUsePointer<Skeleton>& skeleton,
                                 const SkeletalMeshImportOptions& options,
                                 const char* what, const char* name)
        {
            if (!skeleton || skeleton->ImportScale == options.Scale) return options.Scale;

            if (!options.UseSkeletonImportScale)
            {
                PLU_CORE_INFO("{} '{}': baking at the requested scale {}, skeleton '{}' is at {} "
                              "(UseSkeletonImportScale off — source file treated as having its own units)",
                              what, name, options.Scale, skeleton->SkeletonName.CStr(), skeleton->ImportScale);
                return options.Scale;
            }

            PLU_CORE_WARN("{} '{}': baking at skeleton '{}' import scale {} instead of the requested {} "
                          "— the rig defines the space its meshes and clips live in. Turn off "
                          "UseSkeletonImportScale if this file really is in different units",
                          what, name, skeleton->SkeletonName.CStr(), skeleton->ImportScale, options.Scale);
            return skeleton->ImportScale;
        }

        // A transform to pre-multiply into every key of a channel, decomposed so it can be applied
        // to position/rotation/scale keys separately (they have independent timelines, so there is
        // no single matrix timeline to compose). Uniform scale only — see FoldedAncestorTransform.
        struct ChannelPrefix
        {
            Vec3 Translation{0.0f};
            Quaternion Rotation{1.0f, 0.0f, 0.0f, 0.0f}; // (w, x, y, z) identity
            float Scale = 1.0f;
            bool IsIdentity = true;
        };

        // Splits a matrix into translation, rotation and one uniform scale. Non-uniform scale is
        // reported so the caller can say so rather than silently importing a skewed clip.
        ChannelPrefix DecomposePrefix(const Matrix4& m, bool* outNonUniform)
        {
            const Vec3 axisX(m[0]);
            const Vec3 axisY(m[1]);
            const Vec3 axisZ(m[2]);

            const float scaleX = glm::length(axisX);
            const float scaleY = glm::length(axisY);
            const float scaleZ = glm::length(axisZ);

            if (outNonUniform)
                *outNonUniform = std::fabs(scaleX - scaleY) > 1e-4f || std::fabs(scaleX - scaleZ) > 1e-4f;

            ChannelPrefix prefix;
            prefix.Scale = scaleX;
            prefix.Translation = Vec3(m[3]);
            if (scaleX > 1e-8f && scaleY > 1e-8f && scaleZ > 1e-8f)
                prefix.Rotation = glm::quat_cast(glm::mat3(axisX / scaleX, axisY / scaleY, axisZ / scaleZ));
            prefix.IsIdentity = false;
            return prefix;
        }

        // Finds the node an animation channel targets in the source scene.
        const aiNode* FindSceneNode(const aiNode* node, const char* name)
        {
            if (!node) return nullptr;
            if (std::strcmp(node->mName.C_Str(), name) == 0) return node;
            for (UInt32 i = 0; i < node->mNumChildren; ++i)
                if (const aiNode* found = FindSceneNode(node->mChildren[i], name)) return found;
            return nullptr;
        }

        // Collapses the transforms of a channel node's ancestors that the skeleton does not
        // contain into a single prefix to bake into that channel's keys.
        //
        // Exporters wrap a rig in container nodes that carry real transforms and are not bones:
        // Blender parents the whole armature under an `Armature` object holding the -90° X that
        // converts its Z-up world to the engine's Y-up. When a clip is imported against a
        // skeleton that has no such node (typical when the rig came from another file — e.g. a
        // Mixamo rig — while the clip was exported from Blender), dropping that channel loses the
        // rotation and the whole character animates lying on its side. The transform still belongs
        // to the pose, so it is folded into the child channel instead, which is exactly what the
        // node hierarchy would have done to it.
        //
        // Walks up until it meets an ancestor the skeleton *does* know: from there the hierarchies
        // agree and the skeleton applies the transforms itself.
        ChannelPrefix FoldedAncestorTransform(const aiScene* scene, Skeleton& skeleton,
                                              const aiAnimation* animation, const char* channelNodeName)
        {
            const aiNode* node = FindSceneNode(scene->mRootNode, channelNodeName);
            if (!node) return {};

            Matrix4 folded(1.0f);
            bool anyFolded = false;

            for (const aiNode* ancestor = node->mParent; ancestor; ancestor = ancestor->mParent)
            {
                if (skeleton.FindNodeByName(ancestor->mName.C_Str())) break;

                // The clip may animate the container node, in which case its animated value is the
                // one in effect, not the node's rest transform.
                Matrix4 ancestorLocal = AssimpToGLM(ancestor->mTransformation);
                for (UInt32 c = 0; c < animation->mNumChannels; ++c)
                {
                    const aiNodeAnim* channel = animation->mChannels[c];
                    if (std::strcmp(channel->mNodeName.C_Str(), ancestor->mName.C_Str()) != 0) continue;

                    // Only a constant container can be folded — a moving one would have to be
                    // resampled onto every descendant's timeline. Containers holding an axis
                    // conversion are constant; a genuinely animated one means root motion the
                    // skeleton has no node to receive.
                    const bool animated =
                        (channel->mNumPositionKeys > 1 && !LocalMatricesMatch(
                            glm::translate(Matrix4(1.0f), AssimpToGLM(channel->mPositionKeys[0].mValue)),
                            glm::translate(Matrix4(1.0f), AssimpToGLM(
                                channel->mPositionKeys[channel->mNumPositionKeys - 1].mValue)))) ||
                        (channel->mNumRotationKeys > 1 && !LocalMatricesMatch(
                            glm::mat4_cast(AssimpToGLM(channel->mRotationKeys[0].mValue)),
                            glm::mat4_cast(AssimpToGLM(
                                channel->mRotationKeys[channel->mNumRotationKeys - 1].mValue))));
                    if (animated)
                        PLU_CORE_WARN("Node '{}' is animated but absent from skeleton '{}' — folding its "
                                      "first key into the child bones; its motion over time is lost",
                                      ancestor->mName.C_Str(), skeleton.SkeletonName.CStr());

                    ancestorLocal = ComposeChannelBindLocal(channel, 1.0f);
                    break;
                }

                folded = ancestorLocal * folded;
                anyFolded = true;
            }

            if (!anyFolded) return {};

            bool nonUniform = false;
            ChannelPrefix prefix = DecomposePrefix(folded, &nonUniform);
            if (nonUniform)
                PLU_CORE_WARN("Skipped ancestors of channel '{}' carry non-uniform scale — baking its "
                              "uniform part only, the clip may be distorted", channelNodeName);
            return prefix;
        }

        // Derives an asset name from a clip's own name in the source file. Exporters qualify the
        // clip with the object that owns it — Blender writes "Armature|ArmatureAction" — and only
        // the part after the last '|' is the clip name proper.
        //
        // Returns an empty string when the name carries no information, which is the caller's
        // signal to fall back to the source file name: an unnamed clip, or an exporter's
        // boilerplate stamp (Mixamo names every clip it generates "mixamo.com", which would
        // otherwise give every animation in a library the same asset name).
        String ClipAssetName(const aiAnimation* animation)
        {
            String name = animation->mName.C_Str();

            const String::SizeType separator = name.RFind('|');
            if (separator != String::Npos)
                name = name.Substring(separator + 1);

            if (name.IsEmpty()) return {};
            if (name.ToLower() == "mixamo.com") return {};
            return name;
        }

        // Strips characters that are illegal in file names.
        String SanitizeAssetName(String name)
        {
            name.Replace("/", "_");
            name.Replace("\\", "_");
            name.Replace(":", "_");
            name.Replace("|", "_");
            name.Replace(".", "_");
            return name;
        }

        // Scatters an aiMesh's bone weights onto vertices already appended to the buffer,
        // mapping bone names through the skeleton palette. aiVertexWeight vertex ids are
        // mesh-local, so baseVertex is where this mesh's vertices start inside the
        // (possibly merged) vertex buffer.
        void ScatterBoneWeights(aiMesh* mesh, HashMap<String, UInt32>& palette,
                                DynamicArray<SkeletalVertex>& vertices, UInt32 baseVertex)
        {
            for (UInt32 b = 0; b < mesh->mNumBones; ++b)
            {
                aiBone* bone = mesh->mBones[b];
                UInt32* boneIndex = palette.Find(bone->mName.C_Str());
                if (!boneIndex)
                {
                    PLU_CORE_WARN("Bone '{}' not found in skeleton palette — skipping its weights", bone->mName.C_Str());
                    continue;
                }

                for (UInt32 w = 0; w < bone->mNumWeights; ++w)
                {
                    const aiVertexWeight& vw = bone->mWeights[w];
                    const UInt32 vertexIndex = baseVertex + vw.mVertexId;
                    if (vertexIndex >= vertices.Size()) continue;
                    AddBoneInfluence(vertices[vertexIndex], *boneIndex, vw.mWeight);
                }
            }
        }

        // Saves a finished SkeletalMesh under outDir/<sanitized name> and registers its
        // descriptor with the asset manager. Shared by the merged and per-mesh paths.
        bool SaveSkeletalMeshAsset(const SkeletalMesh& mesh, String name, const Path& outDir,
                                   TUsePointer<EngineAssetManager> assetManager)
        {
            name = SanitizeAssetName(name);

            Path savePath = outDir;
            savePath /= name + PLU_BINARY_EXT;

            if (!SaveSkeletalMesh(savePath, mesh))
            {
                PLU_CORE_ERROR("Failed to save skeletal mesh: {}", name.CStr());
                return false;
            }

            PLU_CORE_INFO("Saved skeletal mesh: {} ({} vertices, {} indices, skeleton '{}')",
                          name.CStr(),
                          mesh.MeshData.Vertices.Size(),
                          mesh.MeshData.Indices.Size(),
                          mesh.MeshSkeleton ? mesh.MeshSkeleton->SkeletonName.CStr() : "<none>");

            assetManager->LoadAssetDescriptor(savePath);
            return true;
        }
    }

    // Imports the skinned meshes in the scene as SkeletalMesh assets: geometry is built with
    // the shared MeshProcessing loop, per-vertex bone influences are gathered from aiMesh bones
    // and mapped onto the owning skeleton's bone palette. Without Merge one asset is produced
    // per aiMesh; with Merge all meshes resolving to the same skeleton share one asset (named
    // after the source file, like static-mesh merge).
    void ImportSkeletalMeshes(const aiScene* scene, const Path& outDir,
                              const DynamicArray<TUsePointer<Skeleton>>& skeletons,
                              const SkeletalMeshImportOptions& options,
                              TUsePointer<EngineAssetManager> assetManager,
                              const String& sourceName)
    {
        PLU_PROFILE_SCOPE("ImportSkeletalMeshes");

        // One merge group per skeleton: meshes skinning to different skeletons live in
        // different palettes/spaces, so they can never share a vertex buffer.
        struct MergeGroup
        {
            TUsePointer<Skeleton> GroupSkeleton;
            HashMap<String, UInt32> Palette;
            SkeletalMesh Mesh;
        };
        DynamicArray<MergeGroup> mergeGroups;

        for (UInt32 mi = 0; mi < scene->mNumMeshes; ++mi)
        {
            aiMesh* mesh = scene->mMeshes[mi];
            if (!mesh->HasBones()) continue;

            // Resolve the skeleton this mesh skins to (an explicit override wins).
            HashMap<String, UInt32> palette;
            TUsePointer<Skeleton> skeleton;
            if (options.SkeletonToUse)
            {
                skeleton = options.SkeletonToUse;
                BuildBonePalette(skeleton->RootNode.GetRaw(), palette);
            }
            else
            {
                skeleton = FindSkeletonForMesh(mesh, skeletons, palette);
            }

            if (!skeleton)
            {
                PLU_CORE_WARN("No matching skeleton for skeletal mesh '{}' — skipping", mesh->mName.C_Str());
                continue;
            }

            const float importScale = ResolveImportScale(skeleton, options, "Skeletal mesh", mesh->mName.C_Str());

            if (options.Merge)
            {
                MergeGroup* group = nullptr;
                for (MergeGroup& candidate : mergeGroups)
                    if (candidate.GroupSkeleton == skeleton) { group = &candidate; break; }
                if (!group)
                {
                    mergeGroups.PushBack(MergeGroup());
                    group = &mergeGroups[mergeGroups.Size() - 1];
                    group->GroupSkeleton = skeleton;
                    group->Palette = palette;
                }

                // Append into the shared buffer: ProcessMeshGeometry offsets indices by the
                // current vertex count (isMerging), the weight scatter offsets vertex ids.
                const UInt32 baseVertex = group->Mesh.MeshData.Vertices.Size();
                MeshProcessing::ProcessMeshGeometry(mesh, group->Mesh.MeshData.Vertices,
                                                    group->Mesh.MeshData.Indices,
                                                    group->Mesh.MeshData.MaterialIndex,
                                                    importScale, options.FlipUVs, glm::mat4(1.0f), true);
                ScatterBoneWeights(mesh, group->Palette, group->Mesh.MeshData.Vertices, baseVertex);
                continue;
            }

            SkeletalMesh skeletalMesh;

            // Skeletal vertices stay in mesh-local space (identity transform); bind pose is
            // carried by the skeleton's offset matrices, so baking node transforms here would
            // double-transform. Scale still applies to positions.
            MeshProcessing::ProcessMeshGeometry(mesh, skeletalMesh.MeshData.Vertices,
                                                skeletalMesh.MeshData.Indices,
                                                skeletalMesh.MeshData.MaterialIndex,
                                                importScale, options.FlipUVs, glm::mat4(1.0f), false);

            ScatterBoneWeights(mesh, palette, skeletalMesh.MeshData.Vertices, 0);

            NormalizeBoneWeights(skeletalMesh.MeshData);
            skeletalMesh.MeshSkeleton = skeleton;

            String meshName = mesh->mName.length > 0
                ? String(mesh->mName.C_Str())
                : (String("SkeletalMesh_") + String::FromInt(mi));

            SaveSkeletalMeshAsset(skeletalMesh, meshName, outDir, assetManager);
        }

        // Finalize and save one asset per merge group. A single group takes the source
        // file's name; multiple skeletons disambiguate by appending the skeleton name.
        for (MergeGroup& group : mergeGroups)
        {
            NormalizeBoneWeights(group.Mesh.MeshData);
            group.Mesh.MeshSkeleton = group.GroupSkeleton;

            String meshName = sourceName;
            if (mergeGroups.Size() > 1)
                meshName = meshName + "_" + group.GroupSkeleton->SkeletonName;

            SaveSkeletalMeshAsset(group.Mesh, meshName, outDir, assetManager);
        }
    }

    void ImportAnimations(const aiScene* scene, Path outDir,
                          const DynamicArray<TUsePointer<Skeleton>>& skeletons,
                          const SkeletalMeshImportOptions& options, TUsePointer<EngineAssetManager> assetManager,
                          const String& sourceName)
    {
        // Candidate skeletons to match animations against: the ones imported/reused in this
        // batch, plus an explicit override if the caller supplied one.
        DynamicArray<TUsePointer<Skeleton>> candidates = skeletons;
        if (options.SkeletonToUse) candidates.PushBack(options.SkeletonToUse);

        // With merge on, a skinned mesh in this file already saved an asset named after the
        // source file — a single animation must not reuse that name or it overwrites the mesh
        // binary (multi-animation names carry a _<index> suffix, so they never collide).
        bool meshClaimsSourceName = false;
        if (options.Merge)
            for (UInt32 m = 0; m < scene->mNumMeshes && !meshClaimsSourceName; ++m)
                if (scene->mMeshes[m]->HasBones()) meshClaimsSourceName = true;

        // Asset names already written by this import, so two clips can't claim the same one.
        HashSet<String> usedAnimNames;

        // Drop clips that carry no animation before anything else looks at them. A Blender FBX
        // routinely holds leftover empty stacks next to the real one (a rig round-tripped
        // through Mixamo keeps an `Armature|mixamo.com` stack with channels but zero keys —
        // Assimp reports "ignoring node animation, did not find any transformation key frames").
        // Filtering here also means the _<index> suffix counts the clips we actually import, so
        // a file with one real clip is named after the source file rather than getting a _0.
        DynamicArray<aiAnimation*> usableAnimations;
        usableAnimations.Reserve(scene->mNumAnimations);
        for (UInt32 i = 0; i < scene->mNumAnimations; ++i)
        {
            aiAnimation* animation = scene->mAnimations[i];
            if (animation->mNumChannels > 0 && animation->mDuration > 0.0)
            {
                usableAnimations.PushBack(animation);
                continue;
            }
            PLU_CORE_INFO("Animation '{}' has no keyframes ({} channels, duration {}) — skipping empty clip",
                          animation->mName.C_Str(), animation->mNumChannels, animation->mDuration);
        }

        for (int i = 0; i < static_cast<int>(usableAnimations.Size()); i++) {
            aiAnimation* animation = usableAnimations[i];
            float FPS = animation->mTicksPerSecond;
            float duration = animation->mDuration;
            PLU_CORE_INFO("Animation '{}' FPS: {}, frames total: {}, duration: {}, channels: {}", animation->mName.C_Str(), FPS, duration, duration / FPS, animation->mNumChannels);

            // Resolve which skeleton this animation drives (by channel-node name coverage).
            float coverage = 0.0f;
            TUsePointer<Skeleton> owningSkeleton = FindSkeletonForAnimation(animation, candidates, &coverage);
            if (owningSkeleton)
                PLU_CORE_INFO("Animation '{}' belongs to skeleton '{}' ({:.0f}% channel coverage)",
                              animation->mName.C_Str(), owningSkeleton->SkeletonName.CStr(), coverage * 100.0f);
            else
                PLU_CORE_WARN("Animation '{}' matched no skeleton (no candidate covers its channels)",
                              animation->mName.C_Str());

            if (!owningSkeleton) continue;

            const float importScale = ResolveImportScale(owningSkeleton, options, "Animation",
                                                         animation->mName.C_Str());

            TOwningPointer<Animation> newAnimation = CreateOwning<Animation>();
            newAnimation->Uuid = PluUUID();
            newAnimation->AnimationSkeleton = owningSkeleton;
            newAnimation->FramesAmount = static_cast<int>(animation->mDuration);
            newAnimation->FramesPerSecond = FPS;

            newAnimation->Tracks.Reserve(animation->mNumChannels);

            // Node names already bound to a track this animation, so the LocalMatrix fallback
            // can't reuse a node another channel already owns.
            HashSet<String> claimedNodeNames;

            for (int j = 0; j < animation->mNumChannels; j++) {
                aiNodeAnim* channel = animation->mChannels[j];
                String nodeName = channel->mNodeName.C_Str();

                AnimationTrack newTrack;
                newTrack.Node = owningSkeleton->FindNodeByName(channel->mNodeName.C_Str());

                if (!newTrack.Node) {
                    // Exact name absent (typical of Assimp FBX pivot splitting, e.g. the skeleton
                    // kept `..._$AssimpFbx$_PreRotation` while the channel is `..._$AssimpFbx$_Rotation`).
                    // Fall back to matching the channel's bind-pose local matrix against the skeleton.
                    Matrix4 channelLocal = ComposeChannelBindLocal(channel, importScale);
                    newTrack.Node = FindNodeByLocalMatrix(*owningSkeleton, channelLocal, claimedNodeNames);
                    if (newTrack.Node)
                        PLU_CORE_WARN("Channel '{}' had no name match; bound to node '{}' by LocalMatrix fallback",
                                      channel->mNodeName.C_Str(), newTrack.Node->NodeName.CStr());
                }

                if (!newTrack.Node) {
                    // One unmatched channel is not a reason to throw the clip away: exporters
                    // animate nodes that are simply not part of the rig. Blender writes a channel
                    // for the Armature *object* that parents the bones, and a skeleton imported
                    // from a different file (e.g. the Mixamo rig this clip retargets onto) has no
                    // such node — 65 of 66 channels are still perfectly good animation.
                    //
                    // The channel gets no track of its own, but its transform is not lost: the
                    // channels below it fold it into their keys (FoldedAncestorTransform).
                    PLU_CORE_INFO("Channel '{}' matches no node in skeleton '{}' — no track of its own; "
                                  "its transform is folded into the bones underneath it",
                                  channel->mNodeName.C_Str(), owningSkeleton->SkeletonName.CStr());
                    continue;
                }

                claimedNodeNames.Insert(newTrack.Node->NodeName);

                // Transforms of container nodes the skeleton doesn't have (Blender's `Armature`
                // and its axis conversion) are baked into this channel's keys — without them the
                // clip animates in the exporter's space instead of the rig's.
                const ChannelPrefix prefix = FoldedAncestorTransform(scene, *owningSkeleton,
                                                                     animation, channel->mNodeName.C_Str());
                if (!prefix.IsIdentity)
                    PLU_CORE_INFO("Channel '{}': folding skipped ancestor transform into its keys",
                                  channel->mNodeName.C_Str());

                // Pre-multiplying a prefix P = T_p * R_p * S_p (uniform S_p) onto a key's local
                // T * R * S decomposes cleanly, which is what lets the three key timelines stay
                // independent: uniform scale commutes with rotation, so
                //   P * (T*R*S) = translate(T_p + R_p*(s_p*T)) * (R_p*R) * (s_p*S).
                // Translations are distances in the rig's space, so they additionally take the
                // owning skeleton's import scale exactly like its own bind translations (see
                // ScaleSkeletonInPlace). Rotations are scale-invariant.
                newTrack.LocationKeys.Reserve(channel->mNumPositionKeys);
                for (int p = 0; p < channel->mNumPositionKeys; ++p) {
                    const aiVectorKey* key = channel->mPositionKeys + p;
                    AnimationVectorKey outKey{};
                    outKey.Timestamp = key->mTime;
                    outKey.Value = prefix.Translation
                                 + prefix.Rotation * (AssimpToGLM(key->mValue) * prefix.Scale);
                    outKey.Value *= importScale;
                    newTrack.LocationKeys.PushBack(outKey);
                }
                newTrack.RotationKeys.Reserve(channel->mNumRotationKeys);
                for (int r = 0; r < channel->mNumRotationKeys; ++r) {
                    const aiQuatKey* key = channel->mRotationKeys + r;
                    AnimationQuatKey outKey{};
                    outKey.Timestamp = key->mTime;
                    outKey.Value = prefix.Rotation * AssimpToGLM(key->mValue);
                    newTrack.RotationKeys.PushBack(outKey);
                }
                newTrack.ScaleKeys.Reserve(channel->mNumScalingKeys);
                for (int s = 0; s < channel->mNumScalingKeys; ++s) {
                    const aiVectorKey* key = channel->mScalingKeys + s;
                    AnimationVectorKey outKey{};
                    outKey.Timestamp = key->mTime;
                    outKey.Value = AssimpToGLM(key->mValue) * prefix.Scale;
                    newTrack.ScaleKeys.PushBack(outKey);
                }
                // Assimp emits keys time-sorted, but the sampler's binary search requires it — enforce.
                newTrack.SortKeys();
                newAnimation->Tracks.Insert(newTrack.Node->NodeName, newTrack);
            }

            // Every channel was skipped above — the clip animates nothing in this skeleton, so
            // writing it would only produce an asset that holds the bind pose.
            if (newAnimation->Tracks.Size() == 0)
            {
                PLU_CORE_WARN("Animation '{}' bound no track in skeleton '{}' — not saving it",
                              animation->mName.C_Str(), owningSkeleton->SkeletonName.CStr());
                continue;
            }

            // Preferred name: the clip's own (Blender action names carry far more meaning than
            // "SourceFile_0" when a file holds several clips). Empty when the option is off or
            // the clip has no usable name — then fall back to naming after the source file, where
            // several clips each take a _<index> suffix.
            String animName = options.UseClipNamesForAnimations ? ClipAssetName(animation) : String();
            if (animName.IsEmpty())
            {
                animName = usableAnimations.Size() > 1
                    ? sourceName + "_" + String::FromInt(i)
                    : sourceName;
            }
            animName = SanitizeAssetName(animName);

            // _Anim keeps a clip from clashing with the merged mesh asset that already took the
            // file's name — reachable both from the fallback above and from a clip genuinely
            // named after its file.
            if (meshClaimsSourceName && animName == SanitizeAssetName(sourceName))
                animName = animName + "_Anim";

            // Clip names are not unique the way indices are: Blender is happy to export two
            // actions under one name, and without a suffix the second would overwrite the
            // first's binary.
            if (usedAnimNames.Contains(animName))
                animName = animName + "_" + String::FromInt(i);
            usedAnimNames.Insert(animName);

            Path savePath = outDir;
            savePath /= animName + PLU_BINARY_EXT;

            if (!SaveAnimation(savePath, *newAnimation))
            {
                PLU_CORE_ERROR("Failed to save animation: {}", animName.CStr());
                continue;
            }

            PLU_CORE_INFO("Saved animation: {} ({} tracks, skeleton '{}')",
                          animName.CStr(), newAnimation->Tracks.Size(),
                          owningSkeleton->SkeletonName.CStr());

            assetManager->LoadAssetDescriptor(savePath);
        }
    }

    // =========================================================================
    // Binary skeleton serialization
    // =========================================================================
    namespace
    {
        // Standard binary-asset header magic/version expected by
        // EngineAssetManager::LoadBinaryDescriptor (magic 'PLUA', any known version).
        constexpr UInt32 kAssetMagic = 0x41554C50;      // 'PLUA'
        // v2: attach points appended after the node tree (v1 files simply have none).
        // v3: ImportScale written right after the skeleton name (older files load as 1.0).
        constexpr UInt32 kSkeletonVersion = 3;
        constexpr const char* kSkeletonTypeName = "Skeleton";

        void WriteSkeletonNode(BinaryFileWriter& writer, const SkeletonNode& node)
        {
            const bool isBone = dynamic_cast<const SkeletonBone*>(&node) != nullptr;
            writer.Write<UInt8>(isBone ? 1 : 0);
            writer.WriteString(node.NodeName);
            writer.Write(node.LocalMatrix);
            if (isBone)
                writer.Write(static_cast<const SkeletonBone&>(node).OffsetMatrix);

            // Only count/write valid children so the tree stays balanced on load.
            UInt32 childCount = 0;
            for (const TOwningPointer<SkeletonNode>& child : node.Children)
                if (child) ++childCount;

            writer.Write<UInt32>(childCount);
            for (const TOwningPointer<SkeletonNode>& child : node.Children)
                if (child) WriteSkeletonNode(writer, *child);
        }

        // Reads a full node (type tag + fields + children) and returns it. Mirrors
        // WriteSkeletonNode, so it works for both the root and any child.
        TOwningPointer<SkeletonNode> ReadSkeletonNode(BinaryFileReader& reader)
        {
            UInt8 isBone = 0;
            reader.Read(isBone);

            TOwningPointer<SkeletonNode> node;
            if (isBone) node = CreateOwning<SkeletonBone>();
            else        node = CreateOwning<SkeletonNode>();

            reader.ReadString(node->NodeName);
            reader.Read(node->LocalMatrix);
            if (isBone)
                reader.Read(static_cast<SkeletonBone&>(*node).OffsetMatrix);

            UInt32 childCount = 0;
            reader.Read(childCount);
            node->Children.Clear();
            for (UInt32 i = 0; i < childCount; ++i)
                node->Children.PushBack(ReadSkeletonNode(reader));

            return node;
        }

        template <typename PathT>
        bool SaveSkeletonImpl(const PathT& path, const Skeleton& skeleton)
        {
            PLU_PROFILE_SCOPE("SaveSkeleton");

            BinaryFileWriter writer(path);
            if (!writer.IsOpen()) return false;

            // Standard binary-asset descriptor header (magic/version/typeName/uuid),
            // so EngineAssetManager::LoadBinaryDescriptor can index this file.
            writer.Write<UInt32>(kAssetMagic);
            writer.Write<UInt32>(kSkeletonVersion);
            writer.WriteString(String(kSkeletonTypeName));   // UInt32 length + bytes == typeLength + typeName
            const UInt64 uuid = skeleton.Uuid;
            writer.Write(uuid);

            // Skeleton payload.
            writer.WriteString(skeleton.SkeletonName);
            writer.Write(skeleton.ImportScale);
            const UInt8 hasRoot = skeleton.RootNode ? 1 : 0;
            writer.Write(hasRoot);
            if (skeleton.RootNode)
                WriteSkeletonNode(writer, *skeleton.RootNode);

            // Attach points (v2+). Only count/write valid ones so the count matches what follows.
            UInt32 attachPointCount = 0;
            for (const auto& [name, attachPoint] : skeleton.AttachPoints)
                if (attachPoint) ++attachPointCount;

            writer.Write(attachPointCount);
            for (const auto& [name, attachPoint] : skeleton.AttachPoints)
            {
                if (!attachPoint) continue;
                // The map key is just a copy of AttachPointName (renaming re-keys the map), so
                // only the struct is stored and the map is rebuilt from it on load.
                writer.WriteString(attachPoint->AttachPointName);
                writer.WriteString(attachPoint->ParentNodeName);
                writer.Write(attachPoint->RelativeLocation);
                writer.Write(attachPoint->RelativeRotation);
            }

            return writer.CloseFile();
        }

        template <typename PathT>
        bool LoadSkeletonImpl(const PathT& path, Skeleton& outSkeleton)
        {
            PLU_PROFILE_SCOPE("LoadSkeleton");

            BinaryFileReader reader(path);
            if (!reader.IsOpen()) return false;

            UInt32 magic = 0;
            UInt32 version = 0;
            reader.Read(magic);
            reader.Read(version);
            if (magic != kAssetMagic || version == 0 || version > kSkeletonVersion)
            {
                PLU_CORE_ERROR("Skeleton file has invalid magic/version!");
                return false;
            }

            String typeName;
            reader.ReadString(typeName);
            if (typeName != kSkeletonTypeName)
            {
                PLU_CORE_ERROR("Skeleton file has wrong asset type: {}", typeName.CStr());
                return false;
            }

            UInt64 uuid = 0;
            reader.Read(uuid);
            outSkeleton.Uuid = uuid;

            reader.ReadString(outSkeleton.SkeletonName);

            // v3+. Pre-v3 rigs were baked at whatever scale their import used with no record of
            // it; 1.0 is the only honest answer and matches how they behaved until now.
            outSkeleton.ImportScale = 1.0f;
            if (version >= 3)
                reader.Read(outSkeleton.ImportScale);

            UInt8 hasRoot = 0;
            reader.Read(hasRoot);
            if (hasRoot)
                outSkeleton.RootNode = ReadSkeletonNode(reader);
            else
                outSkeleton.RootNode = TOwningPointer<SkeletonNode>();

            // Attach points, keyed by their own name. Absent in v1 files — those simply load
            // with none, which is exactly what a skeleton authored before them had.
            outSkeleton.AttachPoints.Clear();
            if (version >= 2)
            {
                UInt32 attachPointCount = 0;
                reader.Read(attachPointCount);
                for (UInt32 i = 0; i < attachPointCount && !reader.HasError(); ++i)
                {
                    TOwningPointer<SkeletonAttachPoint> attachPoint = CreateOwning<SkeletonAttachPoint>();
                    reader.ReadString(attachPoint->AttachPointName);
                    reader.ReadString(attachPoint->ParentNodeName);
                    reader.Read(attachPoint->RelativeLocation);
                    reader.Read(attachPoint->RelativeRotation);
                    outSkeleton.AttachPoints.Insert(attachPoint->AttachPointName, attachPoint);
                }
            }

            return !reader.HasError() && reader.CloseFile();
        }

        // =====================================================================
        // Binary skeletal-mesh serialization (same 'PLUA' descriptor header).
        // =====================================================================
        constexpr UInt32 kSkeletalMeshVersion = 1;
        constexpr const char* kSkeletalMeshTypeName = "SkeletalMesh";

        void WriteSkeletalVertex(BinaryFileWriter& writer, const SkeletalVertex& v)
        {
            // Per-field (not a raw struct blob) so the format is independent of Vertex padding.
            writer.Write(v.Position);
            writer.Write(v.Normal);
            writer.WriteArray(v.UV, 2);
            writer.Write(v.Color);
            writer.Write(v.Tangent);
            writer.WriteArray(v.BoneIndices, kMaxBoneInfluence);
            writer.WriteArray(v.BoneWeights, kMaxBoneInfluence);
        }

        // Size of one vertex record as WriteSkeletalVertex lays it out on disk. Derived from the
        // field sizes rather than sizeof(SkeletalVertex) — the format is deliberately padding-
        // independent, so the two are only equal by coincidence.
        constexpr UInt64 kSkeletalVertexDiskSize =
            sizeof(Vec3)                         // Position
            + sizeof(UInt32)                     // Normal
            + sizeof(UInt16) * 2                 // UV
            + sizeof(UInt32)                     // Color
            + sizeof(UInt32)                     // Tangent
            + sizeof(UInt32) * kMaxBoneInfluence // BoneIndices
            + sizeof(float) * kMaxBoneInfluence; // BoneWeights

        // Vertices decoded per BinaryFileReader::Read call. Bounds the scratch buffer instead of
        // mirroring the whole vertex block, which is tens of MB on a real mesh.
        constexpr UInt32 kSkeletalVertexChunk = 8192;

        template <typename T>
        void DecodePacked(const UInt8*& cursor, T& outValue)
        {
            static_assert(std::is_trivially_copyable_v<T>, "DecodePacked requires a trivially copyable type");
            std::memcpy(&outValue, cursor, sizeof(T));
            cursor += sizeof(T);
        }

        template <typename T>
        void DecodePackedArray(const UInt8*& cursor, T* outValues, UInt64 count)
        {
            static_assert(std::is_trivially_copyable_v<T>, "DecodePackedArray requires a trivially copyable type");
            std::memcpy(outValues, cursor, sizeof(T) * count);
            cursor += sizeof(T) * count;
        }

        // Reading counterpart of WriteSkeletalVertex — same field order, but decoded from a memory
        // cursor. A per-field BinaryFileReader::Read costs one call across the shared-library
        // boundary each; at ~1M vertices that was 7M calls and dominated skeletal-mesh load time.
        void DecodeSkeletalVertex(const UInt8*& cursor, SkeletalVertex& v)
        {
            DecodePacked(cursor, v.Position);
            DecodePacked(cursor, v.Normal);
            DecodePackedArray(cursor, v.UV, 2);
            DecodePacked(cursor, v.Color);
            DecodePacked(cursor, v.Tangent);
            DecodePackedArray(cursor, v.BoneIndices, kMaxBoneInfluence);
            DecodePackedArray(cursor, v.BoneWeights, kMaxBoneInfluence);
        }

        // Fills outVertices[0..vertexCount) from the vertex block at the reader's current position.
        // outVertices must already be sized. Reads in chunks of kSkeletalVertexChunk records.
        bool ReadSkeletalVertices(BinaryFileReader& reader, DynamicArray<SkeletalVertex>& outVertices, UInt32 vertexCount)
        {
            if (vertexCount == 0) return true;

            DynamicArray<UInt8> chunk;
            chunk.Resize(static_cast<UInt64>(kSkeletalVertexChunk) * kSkeletalVertexDiskSize);

            for (UInt32 first = 0; first < vertexCount; first += kSkeletalVertexChunk)
            {
                const UInt32 count = vertexCount - first < kSkeletalVertexChunk ? vertexCount - first : kSkeletalVertexChunk;
                if (!reader.Read(chunk.Data(), static_cast<UInt64>(count) * kSkeletalVertexDiskSize)) return false;

                const UInt8* cursor = chunk.Data();
                for (UInt32 i = 0; i < count; ++i)
                    DecodeSkeletalVertex(cursor, outVertices[first + i]);
            }
            return true;
        }

        template <typename PathT>
        bool SaveSkeletalMeshImpl(const PathT& path, const SkeletalMesh& mesh)
        {
            PLU_PROFILE_SCOPE("SaveSkeletalMesh");

            BinaryFileWriter writer(path);
            if (!writer.IsOpen()) return false;

            writer.Write<UInt32>(kAssetMagic);
            writer.Write<UInt32>(kSkeletalMeshVersion);
            writer.WriteString(String(kSkeletalMeshTypeName));
            const UInt64 uuid = mesh.Uuid;
            writer.Write(uuid);

            // Referenced skeleton (by UUID; 0 == none). Resolved on load via the asset manager.
            const UInt64 skeletonUuid = mesh.MeshSkeleton ? static_cast<UInt64>(mesh.MeshSkeleton->Uuid) : 0;
            writer.Write(skeletonUuid);

            const UInt32 vertexCount = mesh.MeshData.Vertices.Size();
            writer.Write(vertexCount);
            for (UInt32 i = 0; i < vertexCount; ++i)
                WriteSkeletalVertex(writer, mesh.MeshData.Vertices[i]);

            const UInt32 indexCount = mesh.MeshData.Indices.Size();
            writer.Write(indexCount);
            writer.WriteArray(mesh.MeshData.Indices.Data(), indexCount);

            writer.Write(mesh.MeshData.MaterialIndex);

            return writer.CloseFile();
        }

        template <typename PathT>
        bool LoadSkeletalMeshImpl(const PathT& path, SkeletalMesh& outMesh, TUsePointer<EngineAssetManager> assetManager)
        {
            PLU_PROFILE_SCOPE("LoadSkeletalMesh");

            BinaryFileReader reader(path);
            if (!reader.IsOpen()) return false;

            UInt32 magic = 0;
            UInt32 version = 0;
            reader.Read(magic);
            reader.Read(version);
            if (magic != kAssetMagic || version != kSkeletalMeshVersion)
            {
                PLU_CORE_ERROR("SkeletalMesh file has invalid magic/version!");
                return false;
            }

            String typeName;
            reader.ReadString(typeName);
            if (typeName != kSkeletalMeshTypeName)
            {
                PLU_CORE_ERROR("SkeletalMesh file has wrong asset type: {}", typeName.CStr());
                return false;
            }

            UInt64 uuid = 0;
            reader.Read(uuid);
            outMesh.Uuid = uuid;

            UInt64 skeletonUuid = 0;
            reader.Read(skeletonUuid);

            UInt32 vertexCount = 0;
            reader.Read(vertexCount);
            outMesh.MeshData.Vertices.Resize(vertexCount);
            if (!ReadSkeletalVertices(reader, outMesh.MeshData.Vertices, vertexCount)) return false;

            UInt32 indexCount = 0;
            reader.Read(indexCount);
            outMesh.MeshData.Indices.Resize(indexCount);
            reader.ReadArray(outMesh.MeshData.Indices.Data(), indexCount);

            reader.Read(outMesh.MeshData.MaterialIndex);

            if (reader.HasError()) return false;

            // Resolve the skeleton reference once the payload has been read.
            if (skeletonUuid != 0 && assetManager)
                outMesh.MeshSkeleton = assetManager->GetAssetData(PluUUID(skeletonUuid));

            return reader.CloseFile();
        }

        // =====================================================================
        // Binary animation serialization (same 'PLUA' descriptor header).
        // =====================================================================
        // v2: per-channel key arrays (v1 stored merged keyframes with channel flags).
        constexpr UInt32 kAnimationVersion = 2;
        constexpr const char* kAnimationTypeName = "Animation";

        // Per-field (not a raw struct blob) so the format is independent of padding.
        void WriteVectorKeys(BinaryFileWriter& writer, const DynamicArray<AnimationVectorKey>& keys)
        {
            writer.Write<UInt32>(static_cast<UInt32>(keys.Size()));
            for (const AnimationVectorKey& key : keys)
            {
                writer.Write(key.Timestamp);
                writer.Write(key.Value);
            }
        }

        void WriteQuatKeys(BinaryFileWriter& writer, const DynamicArray<AnimationQuatKey>& keys)
        {
            writer.Write<UInt32>(static_cast<UInt32>(keys.Size()));
            for (const AnimationQuatKey& key : keys)
            {
                writer.Write(key.Timestamp);
                writer.Write(key.Value);
            }
        }

        void ReadVectorKeys(BinaryFileReader& reader, DynamicArray<AnimationVectorKey>& outKeys)
        {
            UInt32 count = 0;
            reader.Read(count);
            outKeys.Reserve(count);
            for (UInt32 i = 0; i < count; ++i)
            {
                AnimationVectorKey key{};
                reader.Read(key.Timestamp);
                reader.Read(key.Value);
                outKeys.PushBack(key);
            }
        }

        void ReadQuatKeys(BinaryFileReader& reader, DynamicArray<AnimationQuatKey>& outKeys)
        {
            UInt32 count = 0;
            reader.Read(count);
            outKeys.Reserve(count);
            for (UInt32 i = 0; i < count; ++i)
            {
                AnimationQuatKey key{};
                reader.Read(key.Timestamp);
                reader.Read(key.Value);
                outKeys.PushBack(key);
            }
        }

        // v1 compatibility: one merged keyframe stream per track; split it into the
        // per-channel arrays using the stored channel flags.
        void ReadTrackKeysV1(BinaryFileReader& reader, AnimationTrack& track)
        {
            UInt32 keyCount = 0;
            reader.Read(keyCount);
            for (UInt32 k = 0; k < keyCount; ++k)
            {
                double timestamp = 0.0;
                Vec3 location(0.0f);
                Quaternion rotation(1.0f, 0.0f, 0.0f, 0.0f);
                Vec3 scale(1.0f);
                reader.Read(timestamp);
                reader.Read(location);
                reader.Read(rotation);
                reader.Read(scale);
                UInt8 isLoc = 0, isScale = 0, isRot = 0;
                reader.Read(isLoc);
                reader.Read(isScale);
                reader.Read(isRot);

                if (isLoc)
                {
                    AnimationVectorKey key{};
                    key.Timestamp = timestamp;
                    key.Value = location;
                    track.LocationKeys.PushBack(key);
                }
                if (isRot)
                {
                    AnimationQuatKey key{};
                    key.Timestamp = timestamp;
                    key.Value = rotation;
                    track.RotationKeys.PushBack(key);
                }
                if (isScale)
                {
                    AnimationVectorKey key{};
                    key.Timestamp = timestamp;
                    key.Value = scale;
                    track.ScaleKeys.PushBack(key);
                }
            }
            // v1 keyframes were hash-map ordered on disk — the sampler needs time-sorted arrays.
            track.SortKeys();
        }

        template <typename PathT>
        bool SaveAnimationImpl(const PathT& path, const Animation& animation)
        {
            PLU_PROFILE_SCOPE("SaveAnimation");

            BinaryFileWriter writer(path);
            if (!writer.IsOpen()) return false;

            writer.Write<UInt32>(kAssetMagic);
            writer.Write<UInt32>(kAnimationVersion);
            writer.WriteString(String(kAnimationTypeName));
            const UInt64 uuid = animation.Uuid;
            writer.Write(uuid);

            // Referenced skeleton (by UUID; 0 == none). Resolved on load via the asset manager.
            const UInt64 skeletonUuid = animation.AnimationSkeleton
                ? static_cast<UInt64>(animation.AnimationSkeleton->Uuid) : 0;
            writer.Write(skeletonUuid);

            writer.Write(animation.FramesAmount);
            writer.Write(animation.FramesPerSecond);

            const UInt32 trackCount = animation.Tracks.Size();
            writer.Write(trackCount);
            for (const auto& [nodeName, track] : animation.Tracks)
            {
                // Track key == target node name; the node pointer is rebound on load.
                writer.WriteString(nodeName);

                WriteVectorKeys(writer, track.LocationKeys);
                WriteQuatKeys(writer, track.RotationKeys);
                WriteVectorKeys(writer, track.ScaleKeys);
            }

            return writer.CloseFile();
        }

        template <typename PathT>
        bool LoadAnimationImpl(const PathT& path, Animation& outAnimation, TUsePointer<EngineAssetManager> assetManager)
        {
            PLU_PROFILE_SCOPE("LoadAnimation");

            BinaryFileReader reader(path);
            if (!reader.IsOpen()) return false;

            UInt32 magic = 0;
            UInt32 version = 0;
            reader.Read(magic);
            reader.Read(version);
            if (magic != kAssetMagic || version == 0 || version > kAnimationVersion)
            {
                PLU_CORE_ERROR("Animation file has invalid magic/version!");
                return false;
            }

            String typeName;
            reader.ReadString(typeName);
            if (typeName != kAnimationTypeName)
            {
                PLU_CORE_ERROR("Animation file has wrong asset type: {}", typeName.CStr());
                return false;
            }

            UInt64 uuid = 0;
            reader.Read(uuid);
            outAnimation.Uuid = uuid;

            UInt64 skeletonUuid = 0;
            reader.Read(skeletonUuid);

            reader.Read(outAnimation.FramesAmount);
            reader.Read(outAnimation.FramesPerSecond);

            // Resolve the skeleton first so track nodes can be rebound by name.
            TUsePointer<Skeleton> skeleton;
            if (skeletonUuid != 0 && assetManager)
                skeleton = assetManager->GetAssetData(PluUUID(skeletonUuid));
            outAnimation.AnimationSkeleton = skeleton;

            UInt32 trackCount = 0;
            reader.Read(trackCount);
            outAnimation.Tracks.Clear();
            outAnimation.Tracks.Reserve(trackCount);
            for (UInt32 t = 0; t < trackCount; ++t)
            {
                String nodeName;
                reader.ReadString(nodeName);

                AnimationTrack track;
                if (skeleton) track.Node = skeleton->FindNodeByName(nodeName);
                if (!track.Node)
                    PLU_CORE_WARN("Animation track node '{}' not found in skeleton — track left unbound", nodeName.CStr());

                if (version >= 2)
                {
                    ReadVectorKeys(reader, track.LocationKeys);
                    ReadQuatKeys(reader, track.RotationKeys);
                    ReadVectorKeys(reader, track.ScaleKeys);
                }
                else
                {
                    ReadTrackKeysV1(reader, track);
                }

                outAnimation.Tracks.Insert(nodeName, track);
            }

            return !reader.HasError() && reader.CloseFile();
        }
    }

    bool SaveSkeleton(const Path& path, const Skeleton& skeleton)  { return SaveSkeletonImpl(path, skeleton); }
    bool SaveSkeleton(const PathW& path, const Skeleton& skeleton) { return SaveSkeletonImpl(path, skeleton); }
    bool LoadSkeleton(const Path& path, Skeleton& outSkeleton)     { return LoadSkeletonImpl(path, outSkeleton); }
    bool LoadSkeleton(const PathW& path, Skeleton& outSkeleton)    { return LoadSkeletonImpl(path, outSkeleton); }

    bool SaveSkeletalMesh(const Path& path, const SkeletalMesh& mesh) { return SaveSkeletalMeshImpl(path, mesh); }
    bool LoadSkeletalMesh(const Path& path, SkeletalMesh& outMesh, TUsePointer<EngineAssetManager> assetManager)
    {
        return LoadSkeletalMeshImpl(path, outMesh, assetManager);
    }

    bool SaveAnimation(const Path& path, const Animation& animation)  { return SaveAnimationImpl(path, animation); }
    bool SaveAnimation(const PathW& path, const Animation& animation) { return SaveAnimationImpl(path, animation); }
    bool LoadAnimation(const Path& path, Animation& outAnimation, TUsePointer<EngineAssetManager> assetManager)
    {
        return LoadAnimationImpl(path, outAnimation, assetManager);
    }
    bool LoadAnimation(const PathW& path, Animation& outAnimation, TUsePointer<EngineAssetManager> assetManager)
    {
        return LoadAnimationImpl(path, outAnimation, assetManager);
    }
}

bool Plu::ImportSkeletalMesh(Path skeletonPath, Path outDir, TUsePointer<EngineAssetManager> assetManager, SkeletalMeshImportOptions options)
{
    PLU_PROFILE_SCOPE("ImportSkeletalMesh");

    PLU_CORE_INFO("Importing skeletal mesh from: {}", skeletonPath.CStr());

    //EnsureAssimpLoggerAttached();
    MeshProcessing::EnsureAssimpLoggerAttached();
    Assimp::Importer importer;

    // Collapse Assimp's FBX pivot chain. With pivots preserved (the default) each FBX node is
    // split into `<bone>_$AssimpFbx$_Translation/PreRotation/Rotation/Scaling/...` helper nodes;
    // components that are identity at bind pose are dropped from the hierarchy, yet animation
    // still carries channels for them (e.g. skeleton keeps `_PreRotation` but the clip drives
    // `_Rotation`) — so channel names never resolve to a skeleton node. Collapsing bakes the
    // whole chain into one transform per bone, so node and channel names both become the plain
    // bone name (`mixamorig:LeftArm`) and match exactly.
    importer.SetPropertyBool(AI_CONFIG_IMPORT_FBX_PRESERVE_PIVOTS, false);

    UInt32 flags =
       aiProcess_Triangulate |
       aiProcess_JoinIdenticalVertices |
       aiProcess_FlipWindingOrder |
       aiProcess_PopulateArmatureData |
       aiProcess_CalcTangentSpace;

    if (options.FlipUVs)
    {
        flags |= aiProcess_FlipUVs;
    }

    if (options.GenerateNormals)
    {
        flags |= aiProcess_GenNormals;
    }
    const aiScene* scene;
    try {
        scene = importer.ReadFile(skeletonPath.CStr(), flags);
    } catch (...) {
        PLU_ERROR("Error importing mesh at: {}", skeletonPath.CStr());
        return false;
    }

    if (!scene || !scene->mRootNode)
    {
        PLU_CORE_ERROR("Assimp Error: {}", importer.GetErrorString());
        return false;
    }

    if (scene->mFlags & AI_SCENE_FLAGS_INCOMPLETE && !scene->HasAnimations()) {
        PLU_CORE_ERROR("Cannot import asset without animations, skeletons or meshes");
        return false;
    }

    // Skeletons the imported meshes will be linked to. When the caller supplies an explicit
    // skeleton we skip importing/saving new ones and let ImportSkeletalMeshes use the override.
    DynamicArray<TUsePointer<Skeleton>> savedSkeletons;

    if (!options.SkeletonToUse)
    {
        DynamicArray<Skeleton> skeletons;
        //ImportSkeletons(const_cast<aiScene *>(scene), assetManager, &skeletons);
        ImportSkeletonsLegacy(const_cast<aiScene *>(scene), assetManager, &skeletons,
                              options.RebuildBindPoseFromSkinning);

        // Put the rig in the same space as the geometry before anything compares or saves it —
        // dedup (IsIdentical) then correctly treats two different import scales as different
        // skeletons instead of silently reusing the first one. ImportScale records the scale on
        // the asset so later imports against this skeleton bake themselves the same way.
        for (Skeleton& skeleton : skeletons)
        {
            skeleton.ImportScale = options.Scale;
            if (options.Scale != 1.0f)
                ScaleSkeletonInPlace(skeleton.RootNode.GetRaw(), options.Scale);
        }

        // Phase 1: collapse identical skeletons in this batch into one representative each,
        // preferring the bone-richest (tie-breaker) so we don't drop a bone that only one
        // of two otherwise-identical skeletons happens to skin.
        DynamicArray<Skeleton*> representatives;
        for (auto& skeleton : skeletons) {
            bool merged = false;
            for (UInt64 i = 0; i < representatives.Size(); ++i) {
                if (!representatives[i]->IsIdentical(skeleton)) continue;
                if (skeleton.CountBones() > representatives[i]->CountBones()) {
                    PLU_CORE_TRACE("Skeleton '{}' supersedes identical '{}' ({} vs {} bones)",
                        skeleton.SkeletonName.CStr(), representatives[i]->SkeletonName.CStr(),
                        skeleton.CountBones(), representatives[i]->CountBones());
                    representatives[i] = &skeleton;
                } else {
                    PLU_CORE_TRACE("Skipping identical skeleton (batch duplicate): {}", skeleton.SkeletonName.CStr());
                }
                merged = true;
                break;
            }
            if (!merged) representatives.PushBack(&skeleton);
        }

        // With merge on and a single skeleton in the file, the merged mesh is named after the
        // source file — name its skeleton '<mesh>_Skeleton' to match. Renaming before phase 2
        // keeps the on-disk reuse check (which compares names) working across reimports.
        if (options.Merge && representatives.Size() == 1)
            representatives[0]->SkeletonName = SanitizeAssetName(skeletonPath.GetStem()) + "_Skeleton";

        // Phase 2: write each representative (skipping ones already on disk) and keep a handle
        // to the resulting asset so the meshes can reference it.
        for (Skeleton* rep : representatives) {
#ifdef PLU_ENGINE_EDITOR_BUILD
            // Reuse an identical skeleton asset with the same name if one already exists on disk.
            TUsePointer<Skeleton> existingIdentical;
            for (auto& existingDesc : assetManager->GetAllAssetDescriptorsOfType(Skeleton::GetStaticClass())) {
                if (existingDesc->AssetName != rep->SkeletonName) continue;
                TUsePointer<Skeleton> existing = assetManager->GetAssetData(existingDesc);
                if (existing && existing->IsIdentical(*rep)) { existingIdentical = existing; break; }
            }
            if (existingIdentical) {
                PLU_CORE_TRACE("Skipping identical skeleton (existing asset): {}", rep->SkeletonName.CStr());
                savedSkeletons.PushBack(existingIdentical);
                continue;
            }
#endif

            Path skeletonSavePath = outDir;
            skeletonSavePath /= rep->SkeletonName + PLU_BINARY_EXT;
            SaveSkeleton(skeletonSavePath, *rep);
            assetManager->LoadAssetDescriptor(skeletonSavePath);

            TUsePointer<Skeleton> saved = assetManager->GetAssetData(rep->Uuid);
            if (saved) savedSkeletons.PushBack(saved);
        }
    }

    // Phase 3: import the skinned geometry itself, linking each mesh to its skeleton.
    ImportSkeletalMeshes(scene, outDir, savedSkeletons, options, assetManager, skeletonPath.GetStem());

    ImportAnimations(scene, outDir, savedSkeletons, options, assetManager, skeletonPath.GetStem());

    return true;
}
