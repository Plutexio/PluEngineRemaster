//
// Created by Plutex on 2026-03-28.
//

#ifndef PLUENGINE_ASSETBROWSERPANEL_H
#define PLUENGINE_ASSETBROWSERPANEL_H

#include <string>

#include "Panels/EditorPanel.h"
#include "AssetBrowserPanel.generated.h"
#include "PluBase/PluUUID.h"

namespace Plu
{
	class EditorAssetCreator;
	class EditorAssetImporter;

	PLU_CLASS()
	class AssetBrowserPanel : public EditorPanel
	{
		REFLECTION_BODY_ASSETBROWSERPANEL()
	public:
		AssetBrowserPanel();
		~AssetBrowserPanel() override;

		String GetPanelName() override;
		void OnUpdate(float deltaTime) override;
		void OnHide() override;
		void OnShow() override;
	private:
		enum class EAssetDirectory { Assets, Scripts, Shaders, EngineAssets };
		// Modal dialogs driven by the browser. Only one can be pending at a time.
		enum class EBrowserDialog { None, NewFolder, Rename, Delete };

		PluUUID mUUID;

		// ── layout ────────────────────────────────────────────────────────────────
		void DrawToolbar();
		void DrawBreadcrumb();
		void WalkDirectory(const PathW& dirPath);
		void DrawAssetItem(const PathW& path, bool isDirectory);
		void DrawEmptyAreaContextMenu();
		void DrawDialogs();
		void DrawTypeSelectionPopup();

		// ── navigation ────────────────────────────────────────────────────────────
		[[nodiscard]] PathW GetRootPath() const;
		void NavigateTo(const PathW& path);
		void NavigateBack();
		void NavigateForward();
		void NavigateUp();
		// Engine assets ship with the engine — the browser never mutates them.
		[[nodiscard]] bool IsDirectoryReadOnly() const;

		// ── asset creation / import ───────────────────────────────────────────────
		void BeginAssetImport(const PathW& targetDirectory);
		void BeginAssetTypeSelection(const PathW& targetDirectory);

		// ── folder / file operations ──────────────────────────────────────────────
		void OpenDialog(EBrowserDialog dialog, const PathW& targetPath, bool targetIsDirectory);
		void CommitNewFolder();
		void CommitRename();
		void CommitDelete();
		// Moves a file or a whole folder into targetDirectory (drag & drop) and fixes the registry.
		void MoveEntry(const PathW& sourcePath, const PathW& targetDirectory);
		// Shared name validation for the New Folder / Rename dialogs. Fills mDialogError.
		bool ValidateDialogName(const PathW& parentDirectory, const PathW& originalPath);

		// ── callbacks ─────────────────────────────────────────────────────────────
		void OnAssetClicked(const PathW& path, bool isDirectory);
		void OnAssetDoubleClicked(const PathW& path, bool isDirectory);
		void OnAssetContextMenu(const PathW& path, bool isDirectory);

		// ── state ─────────────────────────────────────────────────────────────────
		DynamicArray<TypeInfo*> mAssetTypesForCreation;
		TUsePointer<EditorAssetCreator>  mAssetCreator;
		TUsePointer<EditorAssetImporter> mAssetImporter;
		// Both dispatch "Finito" from inside their own RenderUI(), so they may only be destroyed
		// after that call returns — the callbacks just raise these flags.
		bool  mAssetCreatorFinished  = false;
		bool  mAssetImporterFinished = false;
		bool  mOpenTypeSelection = false;
		PathW mCreationTargetDirectory;

		PathW               mSelectedPath;
		PathW               mCurrentPath;
		// Browser-style history: going somewhere new pushes onto the back stack and drops the
		// forward one; back/forward move entries between the two.
		DynamicArray<PathW> mNavigationHistory;
		DynamicArray<PathW> mForwardHistory;
		EAssetDirectory     mActiveDirectory = EAssetDirectory::Assets;

		// Pending modal dialog. mDialogPath is the parent directory for NewFolder, and the
		// affected entry for Rename/Delete.
		EBrowserDialog mPendingDialog = EBrowserDialog::None;
		bool           mOpenPendingDialog = false;
		PathW          mDialogPath;
		bool           mDialogPathIsDirectory = false;
		std::string    mDialogNameBuffer;
		String         mDialogError;
	};
}

#endif //PLUENGINE_ASSETBROWSERPANEL_H
