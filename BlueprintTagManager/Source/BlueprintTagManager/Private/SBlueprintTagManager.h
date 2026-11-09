#pragma once

#include "BlueprintTagProject.h"
#include "BlueprintTagService.h"
#include "BlueprintTagView.h"
#include "EditorUndoClient.h"
#include "UObject/StrongObjectPtr.h"
#include "Widgets/SCompoundWidget.h"
#include "Widgets/Views/SListView.h"
#include "Widgets/Views/STreeView.h"

class SEditableTextBox;

struct FBlueprintTagInventoryRow
{
    FName Tag;
    int32 Uses = 0;
    int32 OwnersCount = 0;
    FString Owners;
};

class SBlueprintTagManager : public SCompoundWidget, public FEditorUndoClient
{
public:
    SLATE_BEGIN_ARGS(SBlueprintTagManager) {} SLATE_END_ARGS()
    void Construct(const FArguments& InArgs);
    virtual ~SBlueprintTagManager() override;
    virtual void Tick(const FGeometry& Geometry, double CurrentTime, float DeltaTime) override;
    virtual void PostUndo(bool bSuccess) override;
    virtual void PostRedo(bool bSuccess) override;
    bool IsScanInProgress() const { return bScanRequested || bScanning; }
    int32 GetBlueprintCount() const { return Blueprints.Num(); }
    int32 GetTargetCount() const { return Rows.Num(); }
    void SetFilter(const FBlueprintTagFilter& Value);
    const TArray<TSharedPtr<FBlueprintTagOwnerRow>>& GetGroups() const { return Groups; }
    const TArray<TSharedPtr<FBlueprintTagOwnerRow>>& GetMatchingTargets() const { return MatchingRows; }
    bool IsGroupExpanded(const TSharedPtr<FBlueprintTagOwnerRow>& Group) const;
    FReply ExpandGroups(bool bExpand);
    FReply ToggleGroup(TSharedPtr<FBlueprintTagOwnerRow> Group);

private:
    bool CanWork() const;
    void BeginScan();
    void ScanNext();
    void AddBlueprintRows(UBlueprint* Blueprint, const TMap<FString, bool>& Checks);
    void RefreshRows();
    void Filter();
    void RebuildInventory();
    void InvalidatePreview();
    void QueueRefresh();
    void OnPropertyChanged(UObject* Object, FPropertyChangedEvent& Event);
    FReply CheckMatches(bool bChecked);
    FReply MakePreview(EBlueprintTagOperation Operation);
    FReply ApplyPreview();
    FReply CompileChanged();
    FReply SaveChanged();
    FReply OpenBlueprint(TSharedPtr<FBlueprintTagOwnerRow> Item);
    FText GetScanText() const;
    FText GetSelectionText() const;
    TSharedRef<ITableRow> MakeOwnerRow(TSharedPtr<FBlueprintTagOwnerRow> Item, const TSharedRef<STableViewBase>& Owner);
    TSharedRef<ITableRow> MakeInventoryRow(TSharedPtr<FBlueprintTagInventoryRow> Item, const TSharedRef<STableViewBase>& Owner);

    TArray<TStrongObjectPtr<UBlueprint>> Blueprints;
    TArray<FAssetData> ScanAssets;
    TMap<FString, bool> ScanChecks;
    TArray<TSharedPtr<FBlueprintTagOwnerRow>> Rows;
    TArray<TSharedPtr<FBlueprintTagOwnerRow>> MatchingRows;
    TArray<TSharedPtr<FBlueprintTagOwnerRow>> Groups;
    TSet<FString> ExpandedAssets;
    TArray<TSharedPtr<FBlueprintTagInventoryRow>> Inventory;
    TArray<FBlueprintTagAssetChange> PendingChanges;
    TArray<TSharedPtr<FString>> PreviewRows;
    TSet<FSoftObjectPath> ChangedAssets;
    TSharedPtr<STreeView<TSharedPtr<FBlueprintTagOwnerRow>>> OwnerTree;
    TSharedPtr<SListView<TSharedPtr<FBlueprintTagInventoryRow>>> InventoryList;
    TSharedPtr<SListView<TSharedPtr<FString>>> PreviewList;
    TSharedPtr<SEditableTextBox> FolderInput;
    TSharedPtr<SEditableTextBox> OldTagInput;
    TSharedPtr<SEditableTextBox> NewTagInput;
    FBlueprintTagFilter SearchFilter;
    FText Status;
    FString ScanFailures;
    int32 ScanIndex = 0;
    int32 FailedAssets = 0;
    int32 SkippedAssets = 0;
    bool bScanning = false;
    bool bScanRequested = true;
    bool bCanceled = false;
    bool bRefreshRequested = false;
    bool bWasEditable = true;
    FDelegateHandle PropertyHandle;
    FDelegateHandle CompileHandle;
};
