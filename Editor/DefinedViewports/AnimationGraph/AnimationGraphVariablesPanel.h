//
// Created by Plutex on 7/23/26.
//

#ifndef PLUENGINE_ANIMATIONGRAPHVARIABLESPANEL_H
#define PLUENGINE_ANIMATIONGRAPHVARIABLESPANEL_H
#include "EditorViewports/IEditorPanel.h"
#include "AnimationGraphVariablesPanel.generated.h"
#include "PluEngine/Core.h"
#include "PluEngine/Core/Widgets/InlineRename.h"

namespace Plu
{
	struct IAnimationGraphVariable;

	// The graph's variable list — add / rename / delete / select — modelled on the scene's Structure
	// panel. Selecting a row hands the variable to AnimationGraphViewport, which the Details panel
	// then inspects (name field + value control). Rows rename in place: double-click or the context
	// menu opens an InputText; Enter or clicking away commits, Esc cancels.
	//
	// Every mutation here must call PanelChangedAsset() (see Editor/CLAUDE.md — dirty marking).
	PLU_CLASS()
	class AnimationGraphVariablesPanel : public IEditorPanel
	{
		REFLECTION_BODY_ANIMATIONGRAPHVARIABLESPANEL()
	private:
		// Rename target as a use-pointer so it self-clears if the variable is deleted mid-rename.
		TUsePointer<IAnimationGraphVariable> mRenamingVariable;
		ImGuiWidgets::InlineRenameState mRename;

		void BeginRename(const TUsePointer<IAnimationGraphVariable>& variable);
		// Applies mRename.Buffer to `variable` (ignoring empty / unchanged / duplicate names) and
		// ends rename mode. Names are the variable's identity (a "Get Variable" node references one),
		// so they must stay unique within the graph.
		void CommitRename(const TUsePointer<class AnimationGraph>& graph,
		                  const TUsePointer<IAnimationGraphVariable>& variable);
	public:
		AnimationGraphVariablesPanel() = default;
		~AnimationGraphVariablesPanel() override = default;

		String GetPanelName() override;
		void OnClosed() override;
		void OnOpened() override;
		void OnUpdate(float deltaTime) override;
	};
}

#endif //PLUENGINE_ANIMATIONGRAPHVARIABLESPANEL_H
