//
// Created by Plutex on 1/1/26.
//

#ifndef PLUENGINE_EDITORPANEL_H
#define PLUENGINE_EDITORPANEL_H
#include "PluEngine/Core/Objects/EngineObject.h"
#include <imgui.h>
#include <imgui_internal.h>
#include "EditorPanel.generated.h"
#include "PluSTL/Pointers/TUsePointer.h"

namespace Plu
{
	struct EditorAppContext;
	class EditorPanelManager;
	struct ApplicationInfo;

	PLU_CLASS(Abstract)
	class EditorPanel : public EngineObject
	{
		REFLECTION_BODY_EDITORPANEL()
	private:
		bool mIsOpen = true;
		bool mCanClose = false;
		// Engine window id (IWindow::GetWindowID) this panel draws into; 0 = the main window.
		UInt32 mWindowIDToRender = 0;
		// Dispatcher of the window we subscribed to in InitPanel; needed to unsubscribe in the
		// destructor so the "WindowClosed" lambda (capturing this) never outlives the panel.
		TUsePointer<EventDispatcher> mWindowDispatcher;
		EventHandle mWindowClosedHandle = 0;
	protected:
		ApplicationInfo* mApplicationInfo;
		EditorAppContext* mEditorAppContext{};
		EditorPanelManager* mEditorPanelManager;

		void SetCanClose(bool canClose);

		bool BeginPanel();
		void EndPanel();
	public:
		EditorPanel();
		~EditorPanel() override;
		void InitPanel(ApplicationInfo *applicationInfo, EditorPanelManager* panelManager, EditorAppContext* editorAppContext);

		virtual String GetPanelName() = 0;
		virtual void OnShow() = 0;
		virtual void OnUpdate(float deltaTime) = 0;
		virtual void OnHide() = 0;

		UInt32 GetWindowIDToRender() const;
		void SetWindowIDToRender(UInt32 windowID);
	};
}

#endif //PLUENGINE_EDITORPANEL_H