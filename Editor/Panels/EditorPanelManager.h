//
// Created by Plutex on 1/1/26.
//

#ifndef PLUENGINE_EDITORPANELMANAGER_H
#define PLUENGINE_EDITORPANELMANAGER_H
#include "PluSTL/PluSTL_FWD.h"

#include "EditorPanel.h"
#include "PluEngine/Application.h"
#include "PluEngine/Core/Objects/EngineObject.h"
#include "PluEngine/Core/Objects/EngineObjectHandle.h"
#include "PluEngine/Core/Objects/EngineObjectManager.h"
#include "EditorPanelManager.generated.h"
#include "PluEngine/Core/Reflection/ClassPointer.h"

namespace Plu
{
	struct EditorAppContext;
	struct ApplicationInfo;

	PLU_CLASS()
	class EditorPanelManager : public EngineObject
	{
		REFLECTION_BODY_EDITORPANELMANAGER()
	private:
		DynamicArray<TOwningPointer<EditorPanel>> mPanels;
		DynamicArray<TOwningPointer<EditorPanel>> mPanelsToDestroy;
		DynamicArray<TOwningPointer<EditorPanel>> mPanelsToRegister;
		DynamicArray<TOwningPointer<EditorPanel>> mPanelsToDock;
		ApplicationInfo* mApplicationInfo;
		EditorAppContext* mEditorAppContext;

	public:
		EditorPanelManager();
		void Init(ApplicationInfo* applicationInfo, EditorAppContext* editorAppContext);
		~EditorPanelManager() override;

		template<class T>
		TUsePointer<T> AddPanel();

		TUsePointer<EditorPanel> AddPanel(const TypeInfo* PanelClass);
		void ClosePanel(EngineObjectHandle panel);
		TUsePointer<EditorPanel> GetPanelByClass(TClassPointer<EditorPanel> panelClass);

		void DockNewPanels(UInt32 windowID);
		// Sends every panel that was rendering into windowID back to the main window; called when
		// that window closes so its panels reappear instead of vanishing.
		void ReturnPanelsFromWindow(UInt32 windowID);
		[[nodiscard]] const DynamicArray<TOwningPointer<EditorPanel>>& GetPanels() const { return mPanels; }
		// Moves a panel to another editor window: it stops being built for its old window and is
		// queued for docking into the target window's dockspace.
		void MovePanelToWindow(EngineObjectHandle panel, UInt32 targetWindowID);
		void InitNewPanels();
		bool AreTherePanelsToDock() const;

		void Init();
		void OnUpdate(float deltaTime, UInt32 windowID);
		void Shutdown();
	};

	template<class T>
	TUsePointer<T> EditorPanelManager::AddPanel()
	{
		static_assert(std::is_base_of_v<EditorPanel, T>, "T should be derived from EditorPanel");
		return AddPanel(T::GetStaticClass());
	}
}

#endif //PLUENGINE_EDITORPANELMANAGER_H