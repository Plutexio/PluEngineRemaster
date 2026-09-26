//
// Created by Plutex on 7/21/26.
//

#include "NodeGraph/NodeGraphEditor.h"

#include "PluEngine/AssetTypes/NodeGraph/GraphNode.h"
#include "PluEngine/AssetTypes/NodeGraph/NodeGraph.h"
#include "PluEngine/PluTypes.h"
#include "PluEngine/Core/Reflection/ReflectionBase.h"
#include "PluEngine/Core/DiskManager.h"
#include <imgui_internal.h> // BeginDragDropTargetCustom / GetCurrentWindow for the canvas drop target
#include <cfloat>
#include <algorithm>

namespace ed = ax::NodeEditor;

namespace Plu
{
	// ---- pin colors ---------------------------------------------------------------------------
	constexpr float kPinDotRadius = 4.5f;

	static ImVec4 PinColor(const NodePin& pin)
	{
		if (pin.Category == EPinCategory::Flow) return ImVec4(0.45f, 0.85f, 0.45f, 1.0f); // pose/exec wires
		return ImVec4(0.40f, 0.65f, 1.00f, 1.0f);                                          // data
	}

	// Must be called between ed::BeginPin/EndPin.
	//
	// The dot is drawn by hand rather than as a ● glyph so its centre is a known number: the link
	// pivot is set to exactly that point. Both other options are visibly off — the default pivot is
	// the centre of the whole pin row (dot + label), and a pivot *rect* (PinPivotRect over the dot)
	// makes the link land on the rect's edge (Pin::GetClosestPoint), i.e. half a dot to the side.
	// A degenerate min==max rect is a point, which is what ends the wire on the circle itself.
	static void DrawPinDot(const ImVec4& color)
	{
		const float height = ImGui::GetTextLineHeight();
		const ImVec2 origin = ImGui::GetCursorScreenPos();
		ImGui::Dummy(ImVec2(kPinDotRadius * 2.0f, height)); // reserves the layout slot

		const ImVec2 center(origin.x + kPinDotRadius, origin.y + height * 0.5f);
		ImGui::GetWindowDrawList()->AddCircleFilled(center, kPinDotRadius, ImGui::GetColorU32(color), 16);
		ed::PinPivotRect(center, center);
	}

	// Width of one pin row: dot + spacing + label.
	static float PinRowWidth(const NodePin& pin)
	{
		return kPinDotRadius * 2.0f + ImGui::GetStyle().ItemSpacing.x
			+ ImGui::CalcTextSize(pin.Name.CStr()).x;
	}

	// ---- NodeViewRegistry ---------------------------------------------------------------------
	void NodeViewRegistry::Register(const String& typeName, INodeView* view)
	{
		if (view) mViews.InsertOrAssign(typeName, view);
	}

	INodeView* NodeViewRegistry::For(GraphNode* node)
	{
		if (node) {
			if (INodeView** found = mViews.Find(node->GetClass()->TypeName)) return *found;
		}
		return &mDefaultView;
	}

	// ---- DefaultNodeView ----------------------------------------------------------------------
	// Title bar fill. Runs after ed::EndNode(), because only then is the node's rect known — the
	// bar has to span the node's full width, which the header text alone doesn't determine. It goes
	// into the node's background draw list so it stays behind the title and inside the node's
	// layer (drawing it into the regular list would put it over other nodes).
	static void DrawHeaderBackground(ed::NodeId nodeId, const ImVec2& nodeMin, const ImVec2& nodeMax,
	                                 float headerBottom, const ImVec4& color)
	{
		ImDrawList* drawList = ed::GetNodeBackgroundDrawList(nodeId);
		if (!drawList) return;

		// nodeMin/Max are the node's OUTER rect: ed::EndNode ends a group that already contains
		// NodePadding, so the bar must be inset by the border, not expanded by the padding.
		const float halfBorder = ed::GetStyle().NodeBorderWidth * 0.5f;

		const ImVec2 barMin(nodeMin.x + halfBorder, nodeMin.y + halfBorder);
		const ImVec2 barMax(nodeMax.x - halfBorder, headerBottom);
		if (barMax.x <= barMin.x || barMax.y <= barMin.y) return;

		drawList->AddRectFilled(barMin, barMax, ImGui::GetColorU32(color),
			ed::GetStyle().NodeRounding, ImDrawFlags_RoundCornersTop);

		// Hairline parting the bar from the body, same idea as the blueprint sample.
		drawList->AddLine(ImVec2(barMin.x, barMax.y - 0.5f), ImVec2(barMax.x, barMax.y - 0.5f),
			IM_COL32(255, 255, 255, 40), 1.0f);
	}

	void DefaultNodeView::Draw(GraphNode* node, NodeGraphEditor& editor)
	{
		ed::NodeId nodeId = editor.NodeId(node);
		ed::BeginNode(nodeId);
		ImGui::PushID(node);

		// Grouped so the header's rect is one item, whatever an overriding DrawHeader draws.
		ImGui::BeginGroup();
		DrawHeader(node, editor);
		ImGui::EndGroup();
		// Bottom of the title bar; the spacing below is deliberately outside the group, so it reads
		// as a gap between bar and body instead of padding inside the fill.
		const float headerBottom = ImGui::GetItemRectMax().y + 6.0f;
		ImGui::Dummy(ImVec2(0.0f, 10.0f));

		DrawPins(node, editor);
		DrawBody(node, editor); // custom widgets sit under the pins, at the bottom of the node
		ImGui::PopID();
		ed::EndNode();

		// The node is the last item now; its rect is the content box (node padding excluded).
		if (ImGui::IsItemVisible()) {
			DrawHeaderBackground(nodeId, ImGui::GetItemRectMin(), ImGui::GetItemRectMax(),
				headerBottom, HeaderColor(node));
		}
	}

	void DefaultNodeView::DrawHeader(GraphNode* node, NodeGraphEditor& editor)
	{
		// Only the title itself — Draw() measures this group to size the title bar behind it, so
		// anything drawn here grows the bar. Spacing below the bar is added by the caller.
		ImGui::TextUnformatted(node->GetDisplayName().CStr());
	}

	// Two columns: inputs down the left edge, outputs down the right edge (dot outwards on both
	// sides, so wires enter/leave the node horizontally). The node has no width of its own — it
	// hugs its content — so the target width is measured here from the widest pin row pair and the
	// header, and the output column is right-aligned inside it.
	void DefaultNodeView::DrawPins(GraphNode* node, NodeGraphEditor& editor)
	{
		if (node->InputPins.IsEmpty() && node->OutputPins.IsEmpty()) return;

		float inputsWidth = 0.0f;
		for (NodePin& pin : node->InputPins) inputsWidth = std::max(inputsWidth, PinRowWidth(pin));
		float outputsWidth = 0.0f;
		for (NodePin& pin : node->OutputPins) outputsWidth = std::max(outputsWidth, PinRowWidth(pin));

		// Gap keeps the two columns apart on nodes narrower than their header.
		constexpr float kColumnGap = 24.0f;
		const float headerWidth = ImGui::CalcTextSize(node->GetDisplayName().CStr()).x;
		const float width = std::max(headerWidth, inputsWidth + kColumnGap + outputsWidth);

		const float startX = ImGui::GetCursorPosX();

		ImGui::BeginGroup();
		for (NodePin& pin : node->InputPins) {
			ed::BeginPin(editor.PinId(node->Uuid, pin.Name, EPinDirection::Input), ed::PinKind::Input);
			DrawPinDot(editor.GetPinColor(pin));
			ImGui::SameLine();
			ImGui::TextUnformatted(pin.Name.CStr());
			ed::EndPin();
		}
		// Zero-size groups confuse the SameLine below; a dummy keeps the column real when a node
		// has outputs only (e.g. a sampler).
		if (node->InputPins.IsEmpty()) ImGui::Dummy(ImVec2(0.0f, 0.0f));
		ImGui::EndGroup();

		if (!node->OutputPins.IsEmpty()) {
			ImGui::SameLine(0.0f, 0.0f);
			const float columnX = startX + width - outputsWidth;
			ImGui::BeginGroup();
			for (NodePin& pin : node->OutputPins) {
				// Right-align each row inside the column so every dot lands on the same edge.
				ImGui::SetCursorPosX(columnX + (outputsWidth - PinRowWidth(pin)));
				ed::BeginPin(editor.PinId(node->Uuid, pin.Name, EPinDirection::Output), ed::PinKind::Output);
				ImGui::TextUnformatted(pin.Name.CStr());
				ImGui::SameLine();
				DrawPinDot(editor.GetPinColor(pin));
				ed::EndPin();
			}
			ImGui::EndGroup();
		}
	}

	// ---- NodeGraphEditor: ids -----------------------------------------------------------------
	String NodeGraphEditor::PinKey(const PluUUID& nodeUuid, const String& pinName, EPinDirection direction)
	{
		return String::FromInt(nodeUuid.getUUID()) + "|" + String::FromInt(static_cast<UInt64>(direction)) + "|" + pinName;
	}

	String NodeGraphEditor::LinkKey(const NodeLink& link)
	{
		return String::FromInt(link.FromNode.getUUID()) + "|" + link.FromPin
			+ ">" + String::FromInt(link.ToNode.getUUID()) + "|" + link.ToPin;
	}

	uintptr_t NodeGraphEditor::AllocNodeId(const PluUUID& uuid)
	{
		UInt64 key = uuid.getUUID();
		if (uintptr_t* found = mNodeIdByUuid.Find(key)) return *found;
		uintptr_t id = mNextId++;
		mNodeIdByUuid.Insert(key, id);
		mNodeUuidById.Insert(id, key);
		return id;
	}

	uintptr_t NodeGraphEditor::AllocPinId(const PluUUID& nodeUuid, const String& pinName, EPinDirection direction)
	{
		String key = PinKey(nodeUuid, pinName, direction);
		if (uintptr_t* found = mPinIdByKey.Find(key)) return *found;
		uintptr_t id = mNextId++;
		mPinIdByKey.Insert(key, id);
		mPinById.Insert(id, PinRef{ nodeUuid, pinName, direction });
		return id;
	}

	uintptr_t NodeGraphEditor::AllocLinkId(const NodeLink& link)
	{
		String key = LinkKey(link);
		if (uintptr_t* found = mLinkIdByKey.Find(key)) return *found;
		uintptr_t id = mNextId++;
		mLinkIdByKey.Insert(key, id);
		mLinkById.Insert(id, link);
		return id;
	}

	ax::NodeEditor::NodeId NodeGraphEditor::NodeId(const GraphNode* node)
	{
		return ed::NodeId(AllocNodeId(node->Uuid));
	}

	ax::NodeEditor::PinId NodeGraphEditor::PinId(const PluUUID& nodeUuid, const String& pinName, EPinDirection direction)
	{
		return ed::PinId(AllocPinId(nodeUuid, pinName, direction));
	}

	void NodeGraphEditor::MarkModified()
	{
		if (mOnModified && *mOnModified) (*mOnModified)();
	}

	ImVec4 NodeGraphEditor::GetPinColor(const NodePin& pin) const
	{
		ImVec4 color;
		if (mPinColorProvider && mPinColorProvider(pin, color)) return color;
		return PinColor(pin); // built-in default (flow vs data)
	}

	void NodeGraphEditor::SetSpawnedNodePosition(const PluUUID& nodeUuid, const ImVec2& canvasPos)
	{
		mNodePositions[nodeUuid.getUUID()] = canvasPos;
	}

	// ---- NodeGraphEditor: frame ---------------------------------------------------------------
	void NodeGraphEditor::Draw(NodeGraph* graph, const std::function<void()>& onModified)
	{
		if (!graph) return;
		mOnModified = &onModified;

		ApplyStoredPositions(graph);
		DrawNodes(graph);
		DrawLinks(graph);
		HandleCreate(graph);
		HandleDelete(graph);
		HandleCanvasDrop(graph);
		HandleContextMenus(graph);
		SyncSelection();
		CapturePositions(graph);

		mOnModified = nullptr;
	}

	void NodeGraphEditor::DrawNodes(NodeGraph* graph)
	{
		for (TOwningPointer<GraphNode>& node : graph->Nodes) {
			if (!node) continue;
			INodeView* view = mRegistry.For(node.GetRaw());
			view->Draw(node.GetRaw(), *this);
		}
	}

	void NodeGraphEditor::DrawLinks(NodeGraph* graph)
	{
		for (const NodeLink& link : graph->Links) {
			uintptr_t linkId = AllocLinkId(link);
			ed::PinId out = ed::PinId(AllocPinId(link.FromNode, link.FromPin, EPinDirection::Output));
			ed::PinId in  = ed::PinId(AllocPinId(link.ToNode,   link.ToPin,   EPinDirection::Input));
			ed::Link(ed::LinkId(linkId), out, in);
		}
	}

	void NodeGraphEditor::HandleCreate(NodeGraph* graph)
	{
		if (ed::BeginCreate(ImVec4(0.5f, 0.7f, 1.0f, 1.0f), 2.0f)) {
			ed::PinId aId, bId;
			if (ed::QueryNewLink(&aId, &bId)) {
				if (aId && bId) {
					PinRef* ra = mPinById.Find(aId.Get());
					PinRef* rb = mPinById.Find(bId.Get());
					bool ok = false;
					PinRef outRef{}, inRef{};
					if (ra && rb && ra->Direction != rb->Direction) {
						PinRef* out = (ra->Direction == EPinDirection::Output) ? ra : rb;
						PinRef* in  = (ra->Direction == EPinDirection::Input)  ? ra : rb;
						GraphNode* fromNode = graph->FindNode(out->Node);
						GraphNode* toNode   = graph->FindNode(in->Node);
						if (fromNode && toNode && !(out->Node == in->Node)) {
							NodePin* op = fromNode->FindPin(out->Pin, EPinDirection::Output);
							NodePin* ip = toNode->FindPin(in->Pin, EPinDirection::Input);
							if (op && ip && NodePin::CanConnect(*op, *ip)) {
								ok = true;
								outRef = *out;
								inRef  = *in;
							}
						}
					}
					if (ok) {
						if (ed::AcceptNewItem(ImVec4(0.45f, 0.85f, 0.45f, 1.0f), 3.0f)) {
							if (graph->Connect(outRef.Node, outRef.Pin, inRef.Node, inRef.Pin)) MarkModified();
						}
					} else {
						ed::RejectNewItem(ImVec4(1.0f, 0.3f, 0.3f, 1.0f), 2.0f);
					}
				}
			}
		}
		ed::EndCreate();
	}

	void NodeGraphEditor::HandleDelete(NodeGraph* graph)
	{
		if (ed::BeginDelete()) {
			ed::LinkId linkId;
			while (ed::QueryDeletedLink(&linkId)) {
				if (ed::AcceptDeletedItem()) {
					if (NodeLink* link = mLinkById.Find(linkId.Get())) {
						graph->Disconnect(*link);
						MarkModified();
					}
				}
			}
			ed::NodeId nodeId;
			while (ed::QueryDeletedNode(&nodeId)) {
				if (ed::AcceptDeletedItem()) {
					if (UInt64* uuid = mNodeUuidById.Find(nodeId.Get())) {
						graph->RemoveNode(PluUUID(*uuid));
						MarkModified();
					}
				}
			}
		}
		ed::EndDelete();
	}

	void NodeGraphEditor::HandleCanvasDrop(NodeGraph* graph)
	{
		if (!mCanvasDropHandler) return;

		// A whole-canvas drop target: there is no single canvas "item" to attach ImGui::BeginDrag
		// DropTarget to, so we register the editor child window's rect as a custom target. Suspend
		// puts us in the host coordinate space (like the context menus), where ScreenToCanvas maps
		// the mouse position back to graph space for node placement.
		ImGuiWindow* canvasWindow = ImGui::GetCurrentWindow();
		const ImRect canvasRect = canvasWindow->Rect();
		ed::Suspend();
		if (ImGui::BeginDragDropTargetCustom(canvasRect, canvasWindow->ID)) {
			mCanvasDropHandler(ed::ScreenToCanvas(ImGui::GetMousePos()));
			ImGui::EndDragDropTarget();
		}
		ed::Resume();
	}

	// ---- add-node palette ----------------------------------------------------------------------
	//
	// Grouping is derived from the node type hierarchy: a node belongs to the category base it derives
	// from. Matching by name (and walking the whole chain) keeps it working when a family grows an
	// intermediate base — MathAddNode derives MathBinaryNode derives MathGraphNode.
	struct PaletteCategory
	{
		const char* BaseTypeName;
		const char* Label;
		const char* NamePrefix; // stripped from the menu entry: "MathAddNode" reads as "Add"
	};

	static constexpr PaletteCategory kPaletteCategories[] = {
		{ "MathGraphNode",    "Math",      "Math"    },
		{ "VectorGraphNode",  "Vector",    "Vector"  },
		{ "LogicGraphNode",   "Logic",     "Logic"   },
		{ "ConvertGraphNode", "Convert",   "Convert" },
		{ "AnimGraphNode",    "Animation", "Anim"    },
		{ "ParticleModuleNode", "Particle",  "Particle" },
	};

	static const PaletteCategory* PaletteCategoryFor(TypeInfo* type)
	{
		for (TypeInfo* current = type; current != nullptr; current = current->BaseType) {
			for (const PaletteCategory& category : kPaletteCategories) {
				if (current->TypeName == category.BaseTypeName) return &category;
			}
		}
		return nullptr;
	}

	// "MathAddNode" -> "Add", "ConvertIntToFloatNode" -> "Int To Float". The raw reflected type name is
	// what the palette used to show; with a couple of dozen entries it needs to read like a menu.
	static String PaletteLabelFor(TypeInfo* type, const PaletteCategory* category)
	{
		String name = type->TypeName;
		if (category && name.StartsWith(category->NamePrefix)) {
			name = name.Substring(String(category->NamePrefix).Length());
		}
		if (name.Length() > 4 && name.EndsWith("Node")) {
			name = name.Substring(0, name.Length() - 4);
		}
		if (name.IsEmpty()) return type->TypeName;

		// Split CamelCase into words. Digits stay attached to their word ("Vec3" survives).
		String label;
		for (UInt64 i = 0; i < name.Length(); ++i) {
			const char character = name[i];
			if (i > 0 && character >= 'A' && character <= 'Z') label += ' ';
			label += character;
		}
		return label;
	}

	void NodeGraphEditor::DrawAddNodeMenu(NodeGraph* graph)
	{
		ImGui::TextDisabled("Add Node");
		ImGui::Separator();

		static ImGuiTextFilter filter;
		if (ImGui::IsWindowAppearing()) {
			filter.Clear();
			ImGui::SetKeyboardFocusHere();
		}
		filter.Draw("##palette-filter", 180.0f);
		ImGui::Separator();

		// Collected and sorted every time the popup draws: the type map iterates in hash order, and an
		// unsorted menu of two dozen entries is unusable. Cheap — this runs only while the popup is open.
		struct PaletteEntry
		{
			TypeInfo* Type = nullptr;
			String Category;
			String Label;
		};
		DynamicArray<PaletteEntry> entries;
		for (auto entry : *TypeRegistry::GetInstance()->GetTypeMap()) {
			TypeInfo* type = entry.second;
			if (!type || type->IsAbstract) continue;
			if (!graph->AcceptsNodeType(type)) continue;
			if (mPaletteTypeFilter && mPaletteTypeFilter(type)) continue; // offered another way

			const PaletteCategory* category = PaletteCategoryFor(type);
			entries.PushBack({ type, category ? String(category->Label) : String("Other"),
			                   PaletteLabelFor(type, category) });
		}
		entries.Sort([](const PaletteEntry& a, const PaletteEntry& b) {
			const int byCategory = a.Category.Compare(b.Category);
			return byCategory != 0 ? byCategory < 0 : a.Label.Compare(b.Label) < 0;
		});

		const auto spawn = [this, graph](TypeInfo* type) {
			if (GraphNode* node = graph->AddNode(type)) {
				mNodePositions[node->Uuid.getUUID()] = mSpawnCanvasPos;
				MarkModified();
			}
		};

		// While filtering, the categories are noise — show one flat "Category > Label" list instead.
		if (filter.IsActive()) {
			bool any = false;
			for (const PaletteEntry& entry : entries) {
				String row = entry.Category;
				row += " > ";
				row += entry.Label;
				if (!filter.PassFilter(row.CStr())) continue;
				any = true;
				if (ImGui::MenuItem(row.CStr())) spawn(entry.Type);
			}
			if (!any) ImGui::TextDisabled("  (no matches)");
			return;
		}

		String openCategory;
		bool categoryOpen = false;
		for (const PaletteEntry& entry : entries) {
			if (entry.Category != openCategory) {
				if (categoryOpen) ImGui::EndMenu();
				openCategory = entry.Category;
				categoryOpen = ImGui::BeginMenu(openCategory.CStr());
				if (!categoryOpen) continue;
			} else if (!categoryOpen) {
				continue; // this category's submenu is closed — skip its entries
			}
			if (ImGui::MenuItem(entry.Label.CStr())) spawn(entry.Type);
		}
		if (categoryOpen) ImGui::EndMenu();
	}

	void NodeGraphEditor::HandleContextMenus(NodeGraph* graph)
	{
		ed::Suspend();
		if (ed::ShowNodeContextMenu(&mContextNode)) {
			ImGui::OpenPopup("node-ctx");
		} else if (ed::ShowBackgroundContextMenu()) {
			mSpawnCanvasPos = ed::ScreenToCanvas(ImGui::GetMousePos());
			ImGui::OpenPopup("add-node");
		}

		if (ImGui::BeginPopup("node-ctx")) {
			if (ImGui::MenuItem("Delete")) {
				if (UInt64* uuid = mNodeUuidById.Find(mContextNode.Get())) {
					graph->RemoveNode(PluUUID(*uuid));
					MarkModified();
				}
			}
			ImGui::EndPopup();
		}

		if (ImGui::BeginPopup("add-node")) {
			DrawAddNodeMenu(graph);
			// Domain-specific extra entries (e.g. a "Variables" section) at the bottom.
			if (mExtraAddMenu) mExtraAddMenu(mSpawnCanvasPos);
			ImGui::EndPopup();
		}
		ed::Resume();
	}

	void NodeGraphEditor::SyncSelection()
	{
		ed::NodeId selected[1];
		int count = ed::GetSelectedNodes(selected, 1);
		if (count >= 1) {
			if (UInt64* uuid = mNodeUuidById.Find(selected[0].Get())) {
				mSelectedNode = PluUUID(*uuid);
				mHasSelection = true;
				return;
			}
		}
		mHasSelection = false;
	}

	void NodeGraphEditor::ApplyStoredPositions(NodeGraph* graph)
	{
		for (TOwningPointer<GraphNode>& node : graph->Nodes) {
			if (!node) continue;
			UInt64 key = node->Uuid.getUUID();
			if (mPositionApplied.Contains(key)) continue;

			uintptr_t id = AllocNodeId(node->Uuid);
			ImVec2 pos;
			ImVec2* stored = mNodePositions.Find(key);
			if (stored && stored->x < FLT_MAX && stored->y < FLT_MAX) {
				pos = *stored;
			} else {
				// No known layout for this node — cascade so fresh/unpositioned nodes don't stack.
				int n = static_cast<int>(mPositionApplied.Size());
				pos = ImVec2(40.0f + static_cast<float>(n % 6) * 190.0f,
				             40.0f + static_cast<float>(n / 6) * 130.0f);
				mNodePositions[key] = pos;
			}
			ed::SetNodePosition(ed::NodeId(id), pos);
			mPositionApplied.Insert(key, true);
		}
	}

	void NodeGraphEditor::CapturePositions(NodeGraph* graph)
	{
		for (TOwningPointer<GraphNode>& node : graph->Nodes) {
			if (!node) continue;
			UInt64 key = node->Uuid.getUUID();
			// Only capture nodes ed has actually laid out. A node added this frame (context menu runs
			// after DrawNodes) isn't drawn yet — ed::GetNodePosition returns (FLT_MAX, FLT_MAX) for it,
			// which would poison its stored position and fling it to infinity next frame (invisible +
			// breaks "F" navigate-to-content). Its spawn position is already stored; leave it be.
			if (!mPositionApplied.Contains(key)) continue;
			ImVec2 pos = ed::GetNodePosition(ed::NodeId(AllocNodeId(node->Uuid)));
			if (pos.x >= FLT_MAX || pos.y >= FLT_MAX) continue; // node not known to ed this frame
			mNodePositions[key] = pos;
		}
	}

	// ---- layout persistence (editor-owned sidecar) --------------------------------------------
	void NodeGraphEditor::LoadLayout(const StringW& path)
	{
		std::optional<JSON> opt = DiskManager::LoadJson(path);
		if (!opt.has_value()) return;
		const JSON& json = opt.value();
		if (!json.contains("positions")) return;
		for (const JSON& entry : json["positions"]) {
			UInt64 uuid = entry.value("uuid", static_cast<UInt64>(0));
			float x = entry.value("x", 0.0f);
			float y = entry.value("y", 0.0f);
			if (x >= FLT_MAX || y >= FLT_MAX) continue; // guard against previously-poisoned layouts
			mNodePositions[uuid] = ImVec2(x, y);
		}
	}

	void NodeGraphEditor::SaveLayout(const StringW& path) const
	{
		JSON json;
		json["positions"] = JSON::array();
		for (auto entry : mNodePositions) {
			JSON e;
			e["uuid"] = entry.first;
			e["x"] = entry.second.x;
			e["y"] = entry.second.y;
			json["positions"].push_back(e);
		}
		DiskManager::SaveJson(path, json);
	}
}
