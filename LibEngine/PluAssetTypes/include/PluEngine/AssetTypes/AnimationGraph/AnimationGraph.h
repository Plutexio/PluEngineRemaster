//
// Created by Plutex on 7/20/26.
//

#ifndef PLUENGINE_ANIMATIONGRAPH_H
#define PLUENGINE_ANIMATIONGRAPH_H

#include "PluEngine/AssetTypes/NodeGraph/NodeGraph.h"
#include "PluEngine/AssetTypes/AnimationGraph/AnimGraphNode.h"
#include "PluEngine/Core/Reflection/TypeTraits.h"
#include "AnimationGraph.generated.h"

namespace Plu
{
    struct TypeInfo;
    struct AnimVariableNode;

    PLU_STRUCT(Abstract)
    struct PLUASSETTYPES_API IAnimationGraphVariable
    {
        REFLECTION_BODY_IANIMATIONGRAPHVARIABLE()

        virtual void* GetData() = 0;
        [[nodiscard]] virtual JSON Serialize() = 0;
        virtual void DeSerialize(const JSON& jsonData, DeserializationContext* deserializationContext) = 0;
#ifdef PLU_ENGINE_EDITOR_BUILD
        // label is the value widget's ImGui label — the caller passes a hidden one ("##...") since the
        // variable's name already gets its own field in the panel.
        virtual bool DrawEditorControl(const String& label) = 0;
#endif

        // Deep copy (Name/TypeName/PinTypeId + value) into a freshly constructed variable of the
        // same concrete type. Used by AnimGraphVariableStore to build a per-instance value set from
        // the asset's defaults.
        [[nodiscard]] virtual TOwningPointer<IAnimationGraphVariable> Clone() const = 0;

        // Copies this variable's value into dst when expectedPinTypeId matches PinTypeId (the data-pin
        // read path: dst is the destination property's storage, expectedPinTypeId its reflected type).
        // Uses assignment of T (not memcpy), so it also works for non-trivial types like String.
        virtual bool CopyValueTo(void* dst, const String& expectedPinTypeId) const = 0;

        String Name;
        String TypeName;
        // Data-pin type id for a variable node reading this variable. Must equal the reflected type
        // spelling that nodes use for their data pins (GraphNode::BuildDataPinsFromReflection uses a
        // property's reflected type name), because NodePin::CanConnect is an exact string compare —
        // e.g. a Float variable must expose "float", not "Float", to plug into a float Alpha pin.
        // Supplied at registration (RegisterType) and copied onto each created variable; not
        // serialized (the factory re-applies it on create/load).
        String PinTypeId;
    };

    template <typename T>
    struct AnimationGraphVariable : public IAnimationGraphVariable
    {
    private:
        T mValue;
    public:
        void DeSerialize(const JSON &jsonData, DeserializationContext *deserializationContext) override
        {
            // Deserialize straight into mValue — the old code wrote through a null void* and dropped
            // the result, so every loaded variable came back default-constructed.
            TypeSerializer<T>::Deserialize(deserializationContext, jsonData, &mValue);
        }

#ifdef PLU_ENGINE_EDITOR_BUILD
        bool DrawEditorControl(const String& label) override
        {
            return TypeSerializer<T>::EditorControl(&mValue, label);
        }
#endif

        void *GetData() override
        {
            return &mValue;
        }

        [[nodiscard]] JSON Serialize() override
        {
            return TypeSerializer<T>::Serialize(GetData());
        }

        [[nodiscard]] TOwningPointer<IAnimationGraphVariable> Clone() const override
        {
            TOwningPointer<AnimationGraphVariable<T>> clone = CreateOwning<AnimationGraphVariable<T>>();
            clone->Name       = Name;
            clone->TypeName   = TypeName;
            clone->PinTypeId  = PinTypeId;
            clone->mValue     = mValue;
            return clone;
        }

        bool CopyValueTo(void* dst, const String& expectedPinTypeId) const override
        {
            if (expectedPinTypeId != PinTypeId) return false;
            *static_cast<T*>(dst) = mValue;
            return true;
        }
    };

    struct PLUASSETTYPES_API AnimationGraphVariableFactory
    {
        using FactoryFunc = std::function<TOwningPointer<IAnimationGraphVariable>()>;
        struct VariableTypeInfo {
            FactoryFunc CTor;
            Vec3 Color;
            String PinTypeName; // reflected pin type this variable exposes (see IAnimationGraphVariable::PinTypeId)
        };
        static HashMap<String, VariableTypeInfo>& GetFactoryMap();

        // Registers the engine's built-in variable types (Integer/Float/Boolean/String/Vec3). Called
        // once from Application::EngineInit() so Editor and Runtime builds share the same factory —
        // previously this only ran from the editor, so a Runtime build's factory stayed empty and
        // AnimationGraphAssetLoader dropped every variable on load (see its "unknown variable type"
        // warning).
        static void RegisterBuiltInTypes();

        // PinTypeName is the reflected type spelling a variable node exposes so it can wire into
        // matching node data pins (see IAnimationGraphVariable::PinTypeId). It must match how nodes
        // spell that type in their PLU_PROPERTY declarations — e.g. "float", "int", "bool", "Vec3".
        template <typename T>
        static void RegisterType(const String& TypeName, const String& PinTypeName, Vec3 Color)
        {
            // Capture by value: the parameters are references that dangle once RegisterType returns,
            // but the factory runs much later (on "Add Variable").
            VariableTypeInfo newInfo;
            newInfo.Color       = Color;
            newInfo.PinTypeName = PinTypeName;
            newInfo.CTor = [TypeName, PinTypeName]() -> TOwningPointer<IAnimationGraphVariable> {
                TOwningPointer<AnimationGraphVariable<T>> variable = CreateOwning<AnimationGraphVariable<T>>();
                variable->TypeName  = TypeName;
                variable->PinTypeId = PinTypeName;
                return variable;
            };

            GetFactoryMap()[TypeName] = newInfo;
        }

        static VariableTypeInfo* GetVariableTypeInfo(const String& TypeName) {
            if (!GetFactoryMap().Contains(TypeName)) {
                return nullptr;
            }
            return &GetFactoryMap()[TypeName];
        }

        // Registered colour for a reflected data-pin type (e.g. "float" → the Float variable colour),
        // or null when no registered variable type exposes that pin type. Lets node data pins be
        // tinted to match the variables that drive them.
        static const Vec3* GetColorForPinType(const String& PinTypeName) {
            for (auto& entry : GetFactoryMap()) {
                if (entry.second.PinTypeName == PinTypeName) return &entry.second.Color;
            }
            return nullptr;
        }

        static TOwningPointer<IAnimationGraphVariable> CreateVariable(const String& TypeName)
        {
            if (!GetFactoryMap().Contains(TypeName)) {
                return nullptr;
            }
            return GetFactoryMap()[TypeName].CTor();
        }
    };

    // Node graph that drives a skeleton's pose: samplers, blends, bone masks, blend spaces and
    // state transitions, evaluated into a Pose (see PluEngine/Animation/BoneTransform.h).
    //
    // The reusable node-graph machinery lives in PluEngine/NodeGraph/ (GraphNode/NodeGraph/links +
    // NodeGraphSerializer). This asset just specialises it to the animation domain: its nodes derive
    // AnimGraphNode. Nodes + links are (de)serialised by NodeGraphSerializer from the asset loader.
    PLU_STRUCT()
    struct PLUASSETTYPES_API AnimationGraph : NodeGraph
    {
        REFLECTION_BODY_ANIMATIONGRAPH()

        // A graph is authored against one skeleton: node indices in every Pose flowing through it
        // are SkeletonPoseLayout indices of this skeleton, so a graph is only meaningful on meshes
        // sharing it. Used as the evaluation skeleton when the caller doesn't supply one (editor
        // preview), and as the reference for node-side authoring (bone pickers, masks).
        PLU_PROPERTY()
        TUsePointer<Skeleton> TargetSkeleton;

        DynamicArray<TOwningPointer<IAnimationGraphVariable>> Variables;

        // Bumped by the editor on every mutation of the variable list (add/remove/rename/type change)
        // — never serialized. The only signal an AnimGraphInstance has that it must MergeFrom() again
        // instead of trusting its already-bound value set.
        UInt32 VariablesRevision = 0;

        // The graph variable with this name, or null. Variables are addressed by name (their stable
        // identity), so names are kept unique by the editor.
        [[nodiscard]] TUsePointer<IAnimationGraphVariable> FindVariable(const String& name);

        // Constructs an AnimVariableNode bound to `variable`, builds its pins and appends it. Returns
        // a non-owning view (owned by Nodes), or null when `variable` is null. Unlike AddNode(TypeInfo*),
        // this sets the variable reference before building pins (a bare AnimVariableNode has none).
        AnimVariableNode* AddVariableNode(const TUsePointer<IAnimationGraphVariable>& variable);

        // Binds every AnimVariableNode's live Variable pointer from its serialized VariableName and
        // rebuilds its pins. Run after Variables are loaded (they load after the nodes).
        void ResolveVariableReferences();

        // Writes each bound AnimVariableNode's VariableName back from its live Variable (so a renamed
        // variable persists correctly). Run before serializing the graph. Unbound nodes keep their key.
        void SyncVariableNodeNames();

        // Whether this graph may drive `skeleton`. True when TargetSkeleton is unassigned (an
        // unconstrained graph — the callers then supply whatever skeleton they have) or when the
        // two are the same asset. False means the graph's node indices address a different
        // skeleton, so evaluating it would produce a garbage pose.
        [[nodiscard]] bool IsCompatibleWith(const TUsePointer<Skeleton>& skeleton) const;

        // The "Add Node" palette lists non-abstract subclasses of this.
        TypeInfo* GetNodeBaseType() override { return AnimGraphNode::GetStaticClass(); }

        // Evaluates the graph: finds the AnimOutputPoseNode and pulls its pose, which recursively
        // pulls upstream nodes across pose links (AnimGraphNode::EvaluateInputPose). Empty pose
        // when the graph has no output node.
        Pose Evaluate(AnimEvalContext& context);
    };
}

#endif //PLUENGINE_ANIMATIONGRAPH_H
