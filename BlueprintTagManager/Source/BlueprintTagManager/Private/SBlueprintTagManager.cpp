#include "SBlueprintTagManager.h"

#include "Editor.h"
#include "Engine/Blueprint.h"
#include "FileHelpers.h"
#include "GameFramework/Actor.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "Misc/ScopedSlowTask.h"
#include "Misc/PackageName.h"
#include "Styling/AppStyle.h"
#include "Subsystems/AssetEditorSubsystem.h"
#include "UObject/UnrealType.h"
#include "UObject/Package.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SCheckBox.h"
#include "Widgets/Input/SEditableTextBox.h"
#include "Widgets/Input/SSearchBox.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SWrapBox.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/Views/SHeaderRow.h"
#include "Widgets/Views/SExpanderArrow.h"

#define LOCTEXT_NAMESPACE "BlueprintTagManager"

void SBlueprintTagManager::Construct(const FArguments& InArgs)
{
    auto SearchField = [this](const FText& Label, const FText& Hint, FString FBlueprintTagFilter::* Field)
    {
        return SNew(SVerticalBox)
            + SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 2)[ SNew(STextBlock).Text(Label) ]
            + SVerticalBox::Slot().AutoHeight()
            [ SNew(SSearchBox).HintText(Hint).OnTextChanged_Lambda([this, Field](const FText& Value)
                { SearchFilter.*Field = Value.ToString(); Filter(); }) ];
    };
    auto PresenceButton = [this](const FText& Label, EBlueprintTagPresence Value)
    {
        return SNew(SCheckBox).Style(FAppStyle::Get(), "ToggleButtonCheckbox")
            .IsChecked_Lambda([this, Value]() { return SearchFilter.ComponentPresence == Value ? ECheckBoxState::Checked : ECheckBoxState::Unchecked; })
            .OnCheckStateChanged_Lambda([this, Value](ECheckBoxState) { SearchFilter.ComponentPresence = Value; Filter(); })
            [ SNew(STextBlock).Text(Label) ];
    };
    PropertyHandle = FCoreUObjectDelegates::OnObjectPropertyChanged.AddSP(this, &SBlueprintTagManager::OnPropertyChanged);
    if (GEditor)
    {
        GEditor->RegisterForUndo(this);
        CompileHandle = GEditor->OnBlueprintCompiled().AddSP(this, &SBlueprintTagManager::QueueRefresh);
    }
    ChildSlot
    [
        SNew(SBorder).Padding(10)
        [
            SNew(SVerticalBox)
            + SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 5)
            [ SNew(STextBlock).Font(FAppStyle::GetFontStyle("BoldFont")).Text(LOCTEXT("Title", "Project Blueprint Tag Manager")) ]
            + SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 6)
            [ SNew(STextBlock).AutoWrapText(true).Text(LOCTEXT("Help", "Expand a Blueprint to select its Actor Tags or components. Group checkboxes select matching targets, including collapsed children. Preview lists every change before Apply.")) ]
            + SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 5)
            [
                SNew(SHorizontalBox)
                + SHorizontalBox::Slot().FillWidth(1)
                [ SAssignNew(FolderInput, SEditableTextBox)
                    .HintText(LOCTEXT("Folder", "Folder: blank = all project content; e.g. /Game/Characters"))
                    .IsEnabled_Lambda([this]() { return CanWork() && !bScanning; }) ]
                + SHorizontalBox::Slot().AutoWidth().Padding(5, 0)
                [ SNew(SButton).Text(LOCTEXT("Scan", "Scan project")).IsEnabled_Lambda([this]() { return CanWork() && !bScanning; })
                    .OnClicked_Lambda([this]() { bScanRequested = true; return FReply::Handled(); }) ]
                + SHorizontalBox::Slot().AutoWidth()
                [ SNew(SButton).Text(LOCTEXT("CancelScan", "Cancel scan")).IsEnabled_Lambda([this]() { return bScanning; })
                    .OnClicked_Lambda([this]() { bScanning = false; bCanceled = true; Filter(); return FReply::Handled(); }) ]
            ]
            + SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 5)
            [ SNew(STextBlock).AutoWrapText(true).Text(this, &SBlueprintTagManager::GetScanText)
                .ToolTipText_Lambda([this]() { return FText::FromString(ScanFailures); }) ]
            + SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 5)
            [
                SNew(SHorizontalBox)
                + SHorizontalBox::Slot().FillWidth(1).Padding(0, 0, 6, 0)
                [ SearchField(LOCTEXT("BlueprintSearch", "Blueprint search"), LOCTEXT("BlueprintHint", "Blueprint name or path"), &FBlueprintTagFilter::Blueprint) ]
                + SHorizontalBox::Slot().FillWidth(1).Padding(0, 0, 6, 0)
                [ SearchField(LOCTEXT("ComponentSearch", "Component search"), LOCTEXT("ComponentHint", "Component name only"), &FBlueprintTagFilter::Component) ]
                + SHorizontalBox::Slot().FillWidth(1).Padding(0, 0, 6, 0)
                [ SearchField(LOCTEXT("SourceSearch", "Source search"), LOCTEXT("SourceHint", "Source only"), &FBlueprintTagFilter::Source) ]
                + SHorizontalBox::Slot().FillWidth(1)
                [ SearchField(LOCTEXT("TagSearch", "Tag search"), LOCTEXT("TagHint", "Tag values only"), &FBlueprintTagFilter::Tags) ]
            ]
            + SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 5)
            [
                SNew(SWrapBox).UseAllottedSize(true).InnerSlotPadding(FVector2D(6, 4))
                + SWrapBox::Slot().VAlign(VAlign_Center)[ SNew(STextBlock).Text(LOCTEXT("Presence", "Tag filter:")) ]
                + SWrapBox::Slot()[ PresenceButton(LOCTEXT("AllComponents", "All"), EBlueprintTagPresence::All) ]
                + SWrapBox::Slot()[ PresenceButton(LOCTEXT("TaggedComponents", "With tags"), EBlueprintTagPresence::Tagged) ]
                + SWrapBox::Slot()[ PresenceButton(LOCTEXT("UntaggedComponents", "Without tags"), EBlueprintTagPresence::Untagged) ]
                + SWrapBox::Slot().VAlign(VAlign_Center)
                [ SNew(STextBlock).Text_Lambda([this]()
                    {
                        if (SearchFilter.ComponentPresence == EBlueprintTagPresence::Tagged)
                        { return LOCTEXT("TaggedScope", "Tagged Blueprints only; tagged Actor / component targets."); }
                        if (SearchFilter.ComponentPresence == EBlueprintTagPresence::Untagged)
                        { return LOCTEXT("UntaggedScope", "All Blueprints; Actor Tags and untagged components."); }
                        return LOCTEXT("AllScope", "All Blueprints, Actor Tags and components.");
                    }) ]
                + SWrapBox::Slot()
                [ SNew(SButton).Text(LOCTEXT("Refresh", "Refresh tags")).IsEnabled_Lambda([this]() { return CanWork() && !bScanning; })
                    .OnClicked_Lambda([this]() { RefreshRows(); return FReply::Handled(); }) ]
                + SWrapBox::Slot()[ SNew(SButton).Text(LOCTEXT("ExpandAll", "Expand all")).OnClicked(this, &SBlueprintTagManager::ExpandGroups, true) ]
                + SWrapBox::Slot()[ SNew(SButton).Text(LOCTEXT("CollapseAll", "Collapse all")).OnClicked(this, &SBlueprintTagManager::ExpandGroups, false) ]
            ]
            + SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 5)
            [
                SNew(SHorizontalBox)
                + SHorizontalBox::Slot().AutoWidth()
                [ SNew(SButton).Text(LOCTEXT("All", "Check matches")).IsEnabled_Lambda([this]() { return CanWork() && !bScanning; }).OnClicked(this, &SBlueprintTagManager::CheckMatches, true) ]
                + SHorizontalBox::Slot().AutoWidth().Padding(5, 0)
                [ SNew(SButton).Text(LOCTEXT("None", "Uncheck matches")).IsEnabled_Lambda([this]() { return !bScanning; }).OnClicked(this, &SBlueprintTagManager::CheckMatches, false) ]
                + SHorizontalBox::Slot().FillWidth(1).VAlign(VAlign_Center)
                [ SNew(STextBlock).Text(this, &SBlueprintTagManager::GetSelectionText) ]
            ]
            + SVerticalBox::Slot().FillHeight(0.5f)
            [
                SAssignNew(OwnerTree, STreeView<TSharedPtr<FBlueprintTagOwnerRow>>).TreeItemsSource(&Groups)
                .SelectionMode(ESelectionMode::None).OnGenerateRow(this, &SBlueprintTagManager::MakeOwnerRow)
                .OnGetChildren_Lambda([](TSharedPtr<FBlueprintTagOwnerRow> Item, TArray<TSharedPtr<FBlueprintTagOwnerRow>>& Children) { Children.Append(Item->Children); })
                .OnExpansionChanged_Lambda([this](TSharedPtr<FBlueprintTagOwnerRow> Item, bool bExpanded)
                { if (bExpanded) { ExpandedAssets.Add(Item->AssetPath); } else { ExpandedAssets.Remove(Item->AssetPath); } })
                .IsEnabled_Lambda([this]() { return CanWork() && !bScanning; })
                .HeaderRow(SNew(SHeaderRow)
                    + SHeaderRow::Column("Target").DefaultLabel(LOCTEXT("Target", "Blueprint / Actor / Component")).FillWidth(0.35f)
                    + SHeaderRow::Column("Source").DefaultLabel(LOCTEXT("Source", "Source / Matching targets")).FillWidth(0.25f)
                    + SHeaderRow::Column("Tags").DefaultLabel(LOCTEXT("Tags", "Tags / Blueprint summary")).FillWidth(0.4f))
            ]
            + SVerticalBox::Slot().AutoHeight().Padding(0, 6, 0, 3)
            [ SNew(STextBlock).Text(LOCTEXT("Inventory", "Tags across matching targets, including collapsed children (click to fill Existing tag)")) ]
            + SVerticalBox::Slot().FillHeight(0.2f)
            [
                SAssignNew(InventoryList, SListView<TSharedPtr<FBlueprintTagInventoryRow>>).ListItemsSource(&Inventory)
                .SelectionMode(ESelectionMode::Single).OnGenerateRow(this, &SBlueprintTagManager::MakeInventoryRow)
                .OnSelectionChanged_Lambda([this](TSharedPtr<FBlueprintTagInventoryRow> Item, ESelectInfo::Type)
                { if (Item && OldTagInput) { OldTagInput->SetText(FText::FromName(Item->Tag)); } })
            ]
            + SVerticalBox::Slot().AutoHeight().Padding(0, 6, 0, 4)
            [
                SNew(SHorizontalBox)
                + SHorizontalBox::Slot().FillWidth(1).Padding(0, 0, 5, 0)
                [ SAssignNew(OldTagInput, SEditableTextBox).HintText(LOCTEXT("Old", "Existing tag (rename / remove)"))
                    .OnTextChanged_Lambda([this](const FText&) { InvalidatePreview(); }) ]
                + SHorizontalBox::Slot().FillWidth(1)
                [ SAssignNew(NewTagInput, SEditableTextBox).HintText(LOCTEXT("New", "New tag (add / rename)"))
                    .OnTextChanged_Lambda([this](const FText&) { InvalidatePreview(); }) ]
            ]
            + SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 4)
            [
                SNew(SWrapBox).UseAllottedSize(true).InnerSlotPadding(FVector2D(5, 4))
                .IsEnabled_Lambda([this]() { return CanWork() && !bScanning; })
                + SWrapBox::Slot()[ SNew(SButton).Text(LOCTEXT("Add", "Preview add")).OnClicked(this, &SBlueprintTagManager::MakePreview, EBlueprintTagOperation::Add) ]
                + SWrapBox::Slot()[ SNew(SButton).Text(LOCTEXT("Rename", "Preview rename")).OnClicked(this, &SBlueprintTagManager::MakePreview, EBlueprintTagOperation::Rename) ]
                + SWrapBox::Slot()[ SNew(SButton).Text(LOCTEXT("Remove", "Preview remove")).OnClicked(this, &SBlueprintTagManager::MakePreview, EBlueprintTagOperation::Remove) ]
                + SWrapBox::Slot()[ SNew(SButton).Text(LOCTEXT("Clear", "Preview clear")).OnClicked(this, &SBlueprintTagManager::MakePreview, EBlueprintTagOperation::Clear) ]
                + SWrapBox::Slot()[ SNew(SButton).Text(LOCTEXT("Dedupe", "Preview deduplicate")).OnClicked(this, &SBlueprintTagManager::MakePreview, EBlueprintTagOperation::Deduplicate) ]
                + SWrapBox::Slot()[ SNew(SButton).ButtonStyle(FAppStyle::Get(), "PrimaryButton").Text(LOCTEXT("Apply", "Apply preview"))
                    .IsEnabled_Lambda([this]() { return !PendingChanges.IsEmpty(); }).OnClicked(this, &SBlueprintTagManager::ApplyPreview) ]
            ]
            + SVerticalBox::Slot().FillHeight(0.3f).MinHeight(60)
            [
                SAssignNew(PreviewList, SListView<TSharedPtr<FString>>).ListItemsSource(&PreviewRows).SelectionMode(ESelectionMode::None)
                .OnGenerateRow_Lambda([](TSharedPtr<FString> Item, const TSharedRef<STableViewBase>& Owner)
                { return SNew(STableRow<TSharedPtr<FString>>, Owner).Padding(4)[ SNew(STextBlock).Text(FText::FromString(*Item)).AutoWrapText(true) ]; })
            ]
            + SVerticalBox::Slot().AutoHeight().Padding(0, 5, 0, 4)
            [ SNew(STextBlock).AutoWrapText(true).Text_Lambda([this]() { return Status; }) ]
            + SVerticalBox::Slot().AutoHeight()
            [
                SNew(SHorizontalBox)
                + SHorizontalBox::Slot().AutoWidth()
                [ SNew(SButton).Text(LOCTEXT("Compile", "Compile changed")).IsEnabled_Lambda([this]() { return CanWork() && !bScanning && !ChangedAssets.IsEmpty(); })
                    .OnClicked(this, &SBlueprintTagManager::CompileChanged) ]
                + SHorizontalBox::Slot().AutoWidth().Padding(5, 0)
                [ SNew(SButton).Text(LOCTEXT("Save", "Save changed...")).IsEnabled_Lambda([this]() { return CanWork() && !bScanning && !ChangedAssets.IsEmpty(); })
                    .OnClicked(this, &SBlueprintTagManager::SaveChanged) ]
                + SHorizontalBox::Slot().FillWidth(1).VAlign(VAlign_Center)
                [ SNew(STextBlock).Text_Lambda([this]() { return FText::Format(LOCTEXT("Touched", "{0} assets tracked for compile/save; Ctrl+Z undoes the tag batch."), ChangedAssets.Num()); }) ]
            ]
        ]
    ];
    InvalidatePreview();
}

SBlueprintTagManager::~SBlueprintTagManager()
{
    FCoreUObjectDelegates::OnObjectPropertyChanged.Remove(PropertyHandle);
    if (GEditor) { GEditor->OnBlueprintCompiled().Remove(CompileHandle); GEditor->UnregisterForUndo(this); }
}

bool SBlueprintTagManager::CanWork() const { return GEditor && !GEditor->PlayWorld; }
void SBlueprintTagManager::QueueRefresh() { bRefreshRequested = true; }
void SBlueprintTagManager::PostUndo(bool) { QueueRefresh(); }
void SBlueprintTagManager::PostRedo(bool) { QueueRefresh(); }

void SBlueprintTagManager::Tick(const FGeometry& Geometry, double CurrentTime, float DeltaTime)
{
    SCompoundWidget::Tick(Geometry, CurrentTime, DeltaTime);
    const bool bEditable = CanWork();
    if (bWasEditable != bEditable)
    {
        bWasEditable = bEditable;
        if (bScanning) { bScanning = false; bCanceled = true; }
        InvalidatePreview();
        if (bEditable) { QueueRefresh(); }
    }
    if (!bEditable) { return; }
    if (bScanRequested) { bScanRequested = false; BeginScan(); }
    if (bScanning) { ScanNext(); }
    else if (bRefreshRequested) { RefreshRows(); }
}

void SBlueprintTagManager::BeginScan()
{
    FText Error;
    TArray<FAssetData> Assets;
    if (!FBlueprintTagProject::Discover(FolderInput->GetText().ToString(), Assets, Error)) { Status = Error; return; }
    ScanChecks.Reset();
    for (const auto& Row : Rows) { ScanChecks.Add(Row->Key(), Row->bChecked); }
    Blueprints.Reset(); Rows.Reset();
    ScanAssets = MoveTemp(Assets);
    ScanIndex = 0; FailedAssets = 0; SkippedAssets = 0;
    ScanFailures.Reset(); bCanceled = false; bRefreshRequested = false;
    bScanning = !ScanAssets.IsEmpty();
    Filter();
}

void SBlueprintTagManager::ScanNext()
{
    const FAssetData& Asset = ScanAssets[ScanIndex++];
    UBlueprint* Blueprint = Cast<UBlueprint>(Asset.GetAsset());
    if (!Blueprint || (Blueprint->ParentClass && Blueprint->ParentClass->IsChildOf(AActor::StaticClass()) && !FBlueprintTagService::CanEdit(Blueprint)))
    {
        ++FailedAssets;
        ScanFailures += Asset.GetObjectPathString() + TEXT("\n");
    }
    else if (FBlueprintTagService::CanEdit(Blueprint))
    {
        Blueprints.Emplace(Blueprint);
        AddBlueprintRows(Blueprint, ScanChecks);
    }
    else { ++SkippedAssets; }
    if (ScanIndex >= ScanAssets.Num()) { bScanning = false; }
    if (!bScanning || ScanIndex % 16 == 0) { Filter(); }
}

void SBlueprintTagManager::AddBlueprintRows(UBlueprint* Blueprint, const TMap<FString, bool>& Checks)
{
    for (FBlueprintTagTarget& Target : FBlueprintTagService::Gather(Blueprint))
    {
        auto Row = MakeShared<FBlueprintTagOwnerRow>();
        Row->Blueprint = Blueprint;
        Row->AssetPath = Blueprint->GetPathName();
        Row->Target = MoveTemp(Target);
        const bool* Previous = Checks.Find(Row->Key());
        Row->bChecked = Row->Target.bEditable && Previous && *Previous;
        Rows.Add(Row);
    }
}

void SBlueprintTagManager::RefreshRows()
{
    bRefreshRequested = false;
    TMap<FString, bool> Checks;
    for (const auto& Row : Rows) { Checks.Add(Row->Key(), Row->bChecked); }
    Rows.Reset();
    for (const auto& Blueprint : Blueprints) { if (Blueprint.IsValid()) { AddBlueprintRows(Blueprint.Get(), Checks); } }
    Filter();
}

void SBlueprintTagManager::OnPropertyChanged(UObject* Object, FPropertyChangedEvent& Event)
{
    if (!Object) { return; }
    for (const auto& Blueprint : Blueprints)
    {
        if (Blueprint.IsValid() && Object->GetOutermost() == Blueprint->GetOutermost()) { QueueRefresh(); break; }
    }
}

void SBlueprintTagManager::Filter()
{
    FBlueprintTagView::Build(Rows, SearchFilter, Groups, MatchingRows);
    InvalidatePreview(); RebuildInventory();
    if (OwnerTree)
    {
        OwnerTree->RequestTreeRefresh();
        for (const auto& Group : Groups)
        {
            if (ExpandedAssets.Contains(Group->AssetPath)) { OwnerTree->SetItemExpansion(Group, true); }
        }
    }
}

void SBlueprintTagManager::SetFilter(const FBlueprintTagFilter& Value) { SearchFilter = Value; Filter(); }
bool SBlueprintTagManager::IsGroupExpanded(const TSharedPtr<FBlueprintTagOwnerRow>& Group) const
{ return OwnerTree && OwnerTree->IsItemExpanded(Group); }

FReply SBlueprintTagManager::ExpandGroups(bool bExpand)
{
    for (const auto& Group : Groups) { OwnerTree->SetItemExpansion(Group, bExpand); }
    return FReply::Handled();
}

void SBlueprintTagManager::RebuildInventory()
{
    Inventory.Reset();
    TMap<FName, TSharedPtr<FBlueprintTagInventoryRow>> ByTag;
    for (const auto& Row : MatchingRows)
    {
        TSet<FName> Seen;
        for (const FName TagName : Row->Target.Tags)
        {
            auto& Item = ByTag.FindOrAdd(TagName);
            if (!Item) { Item = MakeShared<FBlueprintTagInventoryRow>(); Item->Tag = TagName; }
            ++Item->Uses;
            if (!Seen.Contains(TagName))
            {
                ++Item->OwnersCount;
                if (Item->OwnersCount <= 8) { Item->Owners += Row->AssetPath + TEXT(" : ") + Row->Target.Label + TEXT("\n"); }
                Seen.Add(TagName);
            }
        }
    }
    ByTag.GenerateValueArray(Inventory);
    Inventory.Sort([](const auto& A, const auto& B) { return A->Tag.LexicalLess(B->Tag); });
    if (InventoryList) { InventoryList->RequestListRefresh(); }
}

void SBlueprintTagManager::InvalidatePreview()
{
    PendingChanges.Reset(); PreviewRows.Reset();
    if (PreviewList) { PreviewList->RequestListRefresh(); }
    Status = CanWork() ? LOCTEXT("NeedPreview", "Check targets, then choose an operation to preview. Apply does not save assets.")
        : LOCTEXT("Playing", "Stop Play / Simulate to scan or edit Blueprint defaults.");
}

FText SBlueprintTagManager::GetScanText() const
{
    return FText::FromString(FString::Printf(TEXT("%s | %d / %d Blueprint assets read | %d Actor Blueprints | %d targets | %d non-Actor skipped | %d failed (hover for paths)"),
        bScanning ? TEXT("Scanning") : bCanceled ? TEXT("Canceled: partial results") : TEXT("Scan complete"),
        ScanIndex, ScanAssets.Num(), Blueprints.Num(), Rows.Num(), SkippedAssets, FailedAssets));
}

FText SBlueprintTagManager::GetSelectionText() const
{
    int32 Count = 0;
    TSet<FString> Assets;
    for (const auto& Row : MatchingRows) { if (Row->bChecked && Row->Target.bEditable) { ++Count; Assets.Add(Row->AssetPath); } }
    return FText::Format(LOCTEXT("Selection", "{0} checked / {1} matching targets in {2} Blueprints (collapsed children included)"), Count, MatchingRows.Num(), Assets.Num());
}

FReply SBlueprintTagManager::CheckMatches(bool bChecked)
{
    for (const auto& Row : MatchingRows) { Row->SetChecked(bChecked); }
    InvalidatePreview(); OwnerTree->RequestTreeRefresh();
    return FReply::Handled();
}

FReply SBlueprintTagManager::MakePreview(EBlueprintTagOperation Operation)
{
    InvalidatePreview();
    if (!CanWork() || bScanning) { return FReply::Handled(); }
    FName OldTag, NewTag;
    if ((Operation == EBlueprintTagOperation::Rename || Operation == EBlueprintTagOperation::Remove)
        && !FBlueprintTagService::ParseTag(OldTagInput->GetText().ToString(), OldTag, Status)) { return FReply::Handled(); }
    if ((Operation == EBlueprintTagOperation::Rename || Operation == EBlueprintTagOperation::Add)
        && !FBlueprintTagService::ParseTag(NewTagInput->GetText().ToString(), NewTag, Status)) { return FReply::Handled(); }
    TMap<UBlueprint*, TSet<FString>> Selected;
    for (const auto& Row : MatchingRows)
    { if (Row->bChecked && Row->Target.bEditable && Row->Blueprint.IsValid()) { Selected.FindOrAdd(Row->Blueprint.Get()).Add(Row->Target.Id); } }
    for (const auto& Blueprint : Blueprints)
    {
        const TSet<FString>* Ids = Selected.Find(Blueprint.Get());
        if (!Ids) { continue; }
        auto Changes = FBlueprintTagService::Preview(Blueprint.Get(), *Ids, Operation, OldTag, NewTag);
        if (Changes.IsEmpty()) { continue; }
        for (const auto& Change : Changes)
        {
            PreviewRows.Add(MakeShared<FString>(Blueprint->GetPathName() + TEXT(" : ") + Change.Label + TEXT("\n  ")
                + FBlueprintTagService::FormatTags(Change.Before) + TEXT("  ->  ") + FBlueprintTagService::FormatTags(Change.After)));
        }
        PendingChanges.Add({ Blueprint.Get(), MoveTemp(Changes) });
    }
    PreviewList->RequestListRefresh();
    Status = PendingChanges.IsEmpty() ? LOCTEXT("NoChange", "No changes. Check matching targets and tag names; matching is case-insensitive.")
        : FText::Format(LOCTEXT("Ready", "Preview: {0} targets in {1} Blueprints. Apply is one Undo step; changes are not saved automatically."), PreviewRows.Num(), PendingChanges.Num());
    return FReply::Handled();
}

FReply SBlueprintTagManager::ApplyPreview()
{
    FText Error;
    const bool bSuccess = CanWork() && !bScanning && FBlueprintTagService::ApplyBatch(PendingChanges, Error);
    if (bSuccess) { for (const auto& Item : PendingChanges) { ChangedAssets.Add(FSoftObjectPath(Item.Blueprint.Get())); } }
    RefreshRows();
    Status = bSuccess ? LOCTEXT("Applied", "Applied across all previewed assets. Compile changed, then Save changed. Ctrl+Z undoes the entire tag batch.") : Error;
    return FReply::Handled();
}

FReply SBlueprintTagManager::CompileChanged()
{
    TArray<UBlueprint*> Assets;
    for (const FSoftObjectPath& Path : ChangedAssets) { if (auto* Blueprint = Cast<UBlueprint>(Path.ResolveObject())) { Assets.Add(Blueprint); } }
    Assets.Sort([](const UBlueprint& A, const UBlueprint& B)
    {
        auto Depth = [](const UBlueprint& Blueprint) { int32 Value = 0; for (UClass* C = Blueprint.GeneratedClass; C; C = C->GetSuperClass()) { ++Value; } return Value; };
        return Depth(A) < Depth(B);
    });
    FScopedSlowTask Progress(Assets.Num(), LOCTEXT("Compiling", "Compiling changed Blueprints"));
    Progress.MakeDialog(true);
    int32 Failed = 0, Compiled = 0;
    for (UBlueprint* Blueprint : Assets)
    {
        if (Progress.ShouldCancel()) { break; }
        Progress.EnterProgressFrame(1, FText::FromString(Blueprint->GetPathName()));
        FKismetEditorUtilities::CompileBlueprint(Blueprint);
        ++Compiled;
        if (Blueprint->Status == BS_Error) { ++Failed; }
    }
    RefreshRows();
    Status = FText::Format(LOCTEXT("Compiled", "Compiled {0} / {1} Blueprints; {2} failed. See Compiler Results / Output Log for errors. Save changed when ready."), Compiled, Assets.Num(), Failed);
    return FReply::Handled();
}

FReply SBlueprintTagManager::SaveChanged()
{
    TArray<UPackage*> Packages;
    for (const FSoftObjectPath& Path : ChangedAssets)
    {
        if (const UObject* Object = Path.ResolveObject()) { if (Object->GetOutermost()->IsDirty()) { Packages.AddUnique(Object->GetOutermost()); } }
    }
    if (Packages.IsEmpty()) { Status = LOCTEXT("NothingToSave", "No tracked assets need saving."); return FReply::Handled(); }
    TArray<UPackage*> Failed;
    const auto Result = FEditorFileUtils::PromptForCheckoutAndSave(Packages, true, true, &Failed);
    Status = Result == FEditorFileUtils::PR_Success ? LOCTEXT("Saved", "Changed assets saved.")
        : LOCTEXT("SaveIncomplete", "Save canceled or incomplete. Unsaved changes remain in the editor.");
    return FReply::Handled();
}

FReply SBlueprintTagManager::OpenBlueprint(TSharedPtr<FBlueprintTagOwnerRow> Item)
{
    if (GEditor && Item->Blueprint.IsValid()) { GEditor->GetEditorSubsystem<UAssetEditorSubsystem>()->OpenEditorForAsset(Item->Blueprint.Get()); }
    return FReply::Handled();
}

TSharedRef<ITableRow> SBlueprintTagManager::MakeOwnerRow(TSharedPtr<FBlueprintTagOwnerRow> Item, const TSharedRef<STableViewBase>& Owner)
{
    class SOwnerRow : public SMultiColumnTableRow<TSharedPtr<FBlueprintTagOwnerRow>>
    {
    public:
        SLATE_BEGIN_ARGS(SOwnerRow) {} SLATE_END_ARGS()
        TSharedPtr<FBlueprintTagOwnerRow> Item;
        FSimpleDelegate Changed;
        FOnClicked Open;
        void Construct(const FArguments&, const TSharedRef<STableViewBase>& Table,
            TSharedPtr<FBlueprintTagOwnerRow> InItem, FSimpleDelegate InChanged, FOnClicked InOpen)
        {
            Item = InItem; Changed = InChanged; Open = InOpen;
            SMultiColumnTableRow::Construct(FSuperRowType::FArguments().Padding(4), Table);
        }
        virtual TSharedRef<SWidget> GenerateWidgetForColumn(const FName& Column) override
        {
            if (Column == "Target")
            {
                const FString Label = Item->bGroup ? FPackageName::ObjectPathToObjectName(Item->AssetPath)
                    : Item->Target.Kind == EBlueprintTagTargetKind::Actor ? TEXT("Actor Tags") : Item->Target.Label;
                TSharedRef<SWidget> Name = SNew(STextBlock).Text(FText::FromString(Label))
                    .ToolTipText(FText::FromString(Item->bGroup ? Item->AssetPath : Label)).OverflowPolicy(ETextOverflowPolicy::Ellipsis);
                if (Item->bGroup)
                {
                    Name = SNew(SButton).ButtonStyle(FAppStyle::Get(), "SimpleButton").OnClicked(Open)
                        .ToolTipText(FText::FromString(Item->AssetPath + TEXT("\nClick to open Blueprint")))[ Name ];
                }
                return SNew(SHorizontalBox)
                    + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)[ SNew(SExpanderArrow, SharedThis(this)) ]
                    + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0, 0, 5, 0)
                    [ SNew(SCheckBox).IsEnabled(Item->CanCheck())
                        .ToolTipText(Item->bGroup ? LOCTEXT("GroupCheck", "Select matching Actor / component targets in this Blueprint, including collapsed children.")
                            : LOCTEXT("TargetCheck", "Select this target for batch editing."))
                        .IsChecked_Lambda([this]() { return Item->CheckState(); })
                        .OnCheckStateChanged_Lambda([this](ECheckBoxState State) { Item->SetChecked(State == ECheckBoxState::Checked); Changed.ExecuteIfBound(); }) ]
                    + SHorizontalBox::Slot().FillWidth(1).VAlign(VAlign_Center)[ Name ];
            }
            const FString Text = Item->bGroup
                ? (Column == "Source" ? FString::Printf(TEXT("%d / %d components shown\n%s"), Item->MatchingComponents,
                    Item->TotalComponents, Item->AllTargetsHaveTags() ? TEXT("All targets have tags")
                        : Item->bActorHasTags ? TEXT("Actor tags: Present") : TEXT("Actor tags: None")) : Item->Summary)
                : (Column == "Source" ? Item->Target.Source + (Item->Target.bEditable ? TEXT("") : TEXT(" (read only)"))
                    : FBlueprintTagService::FormatTags(Item->Target.Tags));
            return SNew(STextBlock).Text(FText::FromString(Text)).ToolTipText(FText::FromString(Text)).OverflowPolicy(ETextOverflowPolicy::Ellipsis);
        }
    };
    return SNew(SOwnerRow, Owner, Item, FSimpleDelegate::CreateSP(this, &SBlueprintTagManager::InvalidatePreview), FOnClicked::CreateSP(this, &SBlueprintTagManager::OpenBlueprint, Item));
}

TSharedRef<ITableRow> SBlueprintTagManager::MakeInventoryRow(TSharedPtr<FBlueprintTagInventoryRow> Item, const TSharedRef<STableViewBase>& Owner)
{
    const FText Text = FText::FromString(FString::Printf(TEXT("%s  |  %d uses in %d targets"), *Item->Tag.ToString(), Item->Uses, Item->OwnersCount));
    return SNew(STableRow<TSharedPtr<FBlueprintTagInventoryRow>>, Owner).Padding(4)
        [ SNew(STextBlock).Text(Text).ToolTipText(FText::FromString(Item->Owners)) ];
}

#undef LOCTEXT_NAMESPACE
