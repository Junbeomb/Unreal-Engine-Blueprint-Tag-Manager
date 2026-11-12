#include "SBlueprintTagManager.h"

#include "AssetThumbnail.h"
#include "Components/StaticMeshComponent.h"
#include "Editor.h"
#include "Engine/Blueprint.h"
#include "Engine/StaticMesh.h"
#include "FileHelpers.h"
#include "GameFramework/Actor.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "Misc/ScopedSlowTask.h"
#include "Misc/PackageName.h"
#include "BlueprintTagCompatibility.h"
#include "Subsystems/AssetEditorSubsystem.h"
#include "UObject/UnrealType.h"
#include "UObject/Package.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SCheckBox.h"
#include "Widgets/Input/SEditableTextBox.h"
#include "Widgets/Input/SSearchBox.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/Layout/SSeparator.h"
#include "Widgets/Layout/SSplitter.h"
#include "Widgets/Layout/SWrapBox.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/Views/SHeaderRow.h"
#include "Widgets/Views/SExpanderArrow.h"
#include "Widgets/SNullWidget.h"

#define LOCTEXT_NAMESPACE "BlueprintTagManager"

void SBlueprintTagManager::Construct(const FArguments& InArgs)
{
    ExcludeToggleStyle = FBlueprintTagStyle::Get().GetWidgetStyle<FCheckBoxStyle>("ToggleButtonCheckbox");
    ExcludeToggleStyle.CheckedImage.TintColor = FLinearColor(0.55f, 0.055f, 0.045f);
    ExcludeToggleStyle.CheckedHoveredImage.TintColor = FLinearColor(0.7f, 0.085f, 0.065f);
    ExcludeToggleStyle.CheckedPressedImage.TintColor = FLinearColor(0.4f, 0.035f, 0.025f);
    auto Section = [](const FText& Heading, const FText& Tip, TSharedRef<SWidget> Body) -> TSharedRef<SWidget>
    {
        return SNew(SBorder).BorderImage(FBlueprintTagStyle::GetBrush("ToolPanel.GroupBorder")).Padding(8)
        [
            SNew(SVerticalBox)
            + SVerticalBox::Slot().AutoHeight()
            [ SNew(STextBlock).Font(FBlueprintTagStyle::GetFontStyle("BoldFont")).Text(Heading).ToolTipText(Tip) ]
            + SVerticalBox::Slot().AutoHeight().Padding(0, 5)
            [ SNew(SSeparator) ]
            + SVerticalBox::Slot().FillHeight(1)[ Body ]
        ];
    };
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
        return SNew(SCheckBox).Style(FBlueprintTagStyle::Get(), "ToggleButtonCheckbox")
            .IsChecked_Lambda([this, Value]() { return SearchFilter.ComponentPresence == Value ? ECheckBoxState::Checked : ECheckBoxState::Unchecked; })
            .OnCheckStateChanged_Lambda([this, Value](ECheckBoxState) { SearchFilter.ComponentPresence = Value; Filter(); })
            [ SNew(STextBlock).Text(Label) ];
    };
    auto TypeToggle = [this](const FText& Label, const FText& Tip, bool FBlueprintTagFilter::* Field)
    {
        const FCheckBoxStyle* ToggleStyle = Field == &FBlueprintTagFilter::bStaticMeshOnly
            ? &FBlueprintTagStyle::Get().GetWidgetStyle<FCheckBoxStyle>("ToggleButtonCheckbox") : &ExcludeToggleStyle;
        return SNew(SCheckBox).Style(ToggleStyle)
            .IsChecked_Lambda([this, Field]() { return SearchFilter.*Field ? ECheckBoxState::Checked : ECheckBoxState::Unchecked; })
            .OnCheckStateChanged_Lambda([this, Field](ECheckBoxState State) { SearchFilter.*Field = State == ECheckBoxState::Checked; Filter(); })
            .ToolTipText(Tip)[ SNew(STextBlock).Text(Label) ];
    };
    auto InputAction = [](const FText& Heading, TSharedRef<SWidget> Input, TSharedRef<SWidget> Button) -> TSharedRef<SWidget>
    {
        return SNew(SBorder).BorderImage(FBlueprintTagStyle::GetBrush("ToolPanel.DarkGroupBorder")).Padding(6)
        [
            SNew(SVerticalBox)
            + SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 4)
            [ SNew(STextBlock).Font(FBlueprintTagStyle::GetFontStyle("BoldFont")).Text(Heading) ]
            + SVerticalBox::Slot().AutoHeight()
            [ SNew(SHorizontalBox)
                + SHorizontalBox::Slot().FillWidth(1).VAlign(VAlign_Center)[ Input ]
                + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(6, 0, 0, 0)[ Button ] ]
        ];
    };
    auto ThumbnailToggle = [this](const FText& Label, const FText& Tip, bool FBlueprintTagThumbnailOptions::* Field)
    {
        return SNew(SCheckBox).Style(FBlueprintTagStyle::Get(), "ToggleButtonCheckbox")
            .IsChecked_Lambda([this, Field]() { return ThumbnailOptions.*Field ? ECheckBoxState::Checked : ECheckBoxState::Unchecked; })
            .OnCheckStateChanged_Lambda([this, Field](ECheckBoxState State)
                {
                    auto Options = ThumbnailOptions;
                    Options.*Field = State == ECheckBoxState::Checked;
                    SetThumbnailOptions(Options);
                })
            .ToolTipText(Tip)[ SNew(STextBlock).Text(Label) ];
    };
    auto ThumbnailSizeButton = [this](int32 Size)
    {
        return SNew(SCheckBox).Style(FBlueprintTagStyle::Get(), "ToggleButtonCheckbox")
            .IsChecked_Lambda([this, Size]() { return ThumbnailOptions.Size == Size ? ECheckBoxState::Checked : ECheckBoxState::Unchecked; })
            .OnCheckStateChanged_Lambda([this, Size](ECheckBoxState)
                {
                    auto Options = ThumbnailOptions;
                    Options.Size = Size;
                    SetThumbnailOptions(Options);
                })
            .ToolTipText(LOCTEXT("ThumbnailSizeHelp", "Thumbnail size in pixels for both Blueprint and Static Mesh images. Display settings do not change tag filters or checked targets."))
            [ SNew(STextBlock).Text(FText::FromString(FString::Printf(TEXT("%dx%d"), Size, Size))) ];
    };
    auto StandaloneAction = [](const FText& Label, const FText& Tip, TSharedRef<SWidget> Button) -> TSharedRef<SWidget>
    {
        return SNew(SBorder).BorderImage(FBlueprintTagStyle::GetBrush("ToolPanel.DarkGroupBorder")).Padding(6).ToolTipText(Tip)
        [ SNew(SHorizontalBox)
            + SHorizontalBox::Slot().FillWidth(1).VAlign(VAlign_Center)
            [ SNew(STextBlock).AutoWrapText(true).Text(Label) ]
            + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(6, 0, 0, 0)[ Button ] ];
    };
    PropertyHandle = FCoreUObjectDelegates::OnObjectPropertyChanged.AddSP(this, &SBlueprintTagManager::OnPropertyChanged);
    if (GEditor)
    {
        GEditor->RegisterForUndo(this);
        CompileHandle = GEditor->OnBlueprintCompiled().AddSP(this, &SBlueprintTagManager::QueueRefresh);
    }

    const auto ScanPanel = Section(LOCTEXT("ScanHeading", "Project scan"),
        LOCTEXT("ScanTip", "Choose the content scope, then click Scan project. Opening this panel does not scan."),
        SNew(SScrollBox)
        + SScrollBox::Slot()
        [
            SNew(SVerticalBox)
            + SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 3)
            [ SNew(STextBlock).Text(LOCTEXT("FolderLabel", "Content folder")) ]
            + SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 5)
            [ SAssignNew(FolderInput, SEditableTextBox)
                .HintText(LOCTEXT("Folder", "Blank = all project content; e.g. /Game/Characters"))
                .IsEnabled_Lambda([this]() { return CanWork() && !IsScanInProgress(); }) ]
            + SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 5)
            [
                SNew(SWrapBox).UseAllottedSize(true).InnerSlotPadding(FVector2D(5, 4))
                + SWrapBox::Slot()
                [ SNew(SButton).ButtonStyle(FBlueprintTagStyle::Get(), "PrimaryButton").Text(LOCTEXT("Scan", "Scan project"))
                    .IsEnabled_Lambda([this]() { return CanWork() && !IsScanInProgress(); })
                    .OnClicked(this, &SBlueprintTagManager::RequestScan) ]
                + SWrapBox::Slot()
                [ SNew(SButton).Text(LOCTEXT("CancelScan", "Cancel scan")).IsEnabled_Lambda([this]() { return bScanning; })
                    .OnClicked_Lambda([this]() { bScanning = false; bCanceled = true; Filter(); return FReply::Handled(); }) ]
            ]
            + SVerticalBox::Slot().AutoHeight()
            [ SNew(STextBlock).AutoWrapText(true).Text(this, &SBlueprintTagManager::GetScanText)
                .ToolTipText_Lambda([this]() { return FText::FromString(ScanFailures); }) ]
        ]);

    const auto SearchPanel = Section(LOCTEXT("SearchHeading", "Search"),
        LOCTEXT("SearchTip", "All nonempty fields and space-separated terms must match. Search does not scan content."),
        SNew(SScrollBox)
        + SScrollBox::Slot()
        [
            SNew(SVerticalBox)
            + SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 6)
            [ SearchField(LOCTEXT("BlueprintSearch", "Blueprint search"), LOCTEXT("BlueprintHint", "Blueprint name or path"), &FBlueprintTagFilter::Blueprint) ]
            + SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 6)
            [ SearchField(LOCTEXT("ComponentSearch", "Component search"), LOCTEXT("ComponentHint", "Component name only"), &FBlueprintTagFilter::Component) ]
            + SVerticalBox::Slot().AutoHeight()
            [ SearchField(LOCTEXT("TagSearch", "Tag search"), LOCTEXT("TagHint", "Tag values only"), &FBlueprintTagFilter::Tags) ]
        ]);

    const auto FilterPanel = Section(LOCTEXT("FilterHeading", "Filters"),
        LOCTEXT("FilterTip", "Type and tag-presence filters combine with the search fields. Excluded targets are excluded from batch edits."),
        SNew(SScrollBox)
        + SScrollBox::Slot()
        [
            SNew(SVerticalBox)
            + SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Left).Padding(0, 0, 0, 5)
            [ TypeToggle(LOCTEXT("MeshOnly", "StaticMesh only"),
                LOCTEXT("MeshOnlyHelp", "Show StaticMeshComponent and derived types. The independent HISM / ISM exclusions below still apply. Components (shown) summarizes matching components; Actor Tags stay separate."),
                &FBlueprintTagFilter::bStaticMeshOnly) ]
            + SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Left).Padding(0, 0, 0, 5)
            [ TypeToggle(LOCTEXT("ExcludeHISM", "Exclude HISM"),
                LOCTEXT("ExcludeHISMHelp", "Exclude Hierarchical Instanced Static Mesh components and their subclasses. Works with StaticMesh only on or off; does not exclude non-hierarchical ISM components."),
                &FBlueprintTagFilter::bExcludeHISM) ]
            + SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Left).Padding(0, 0, 0, 6)
            [ TypeToggle(LOCTEXT("ExcludeISM", "Exclude ISM"),
                LOCTEXT("ExcludeISMHelp", "Exclude Instanced Static Mesh components and their non-hierarchical subclasses. Works with StaticMesh only on or off. HISM is controlled separately above."),
                &FBlueprintTagFilter::bExcludeISM) ]
            + SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 3)
            [ SNew(STextBlock).Text(LOCTEXT("Presence", "Tag presence")) ]
            + SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 5)
            [
                SNew(SWrapBox).UseAllottedSize(true).InnerSlotPadding(FVector2D(5, 4))
                + SWrapBox::Slot()[ PresenceButton(LOCTEXT("AllComponents", "All"), EBlueprintTagPresence::All) ]
                + SWrapBox::Slot()[ PresenceButton(LOCTEXT("TaggedComponents", "With tags"), EBlueprintTagPresence::Tagged) ]
                + SWrapBox::Slot()[ PresenceButton(LOCTEXT("UntaggedComponents", "Without tags"), EBlueprintTagPresence::Untagged) ]
            ]
            + SVerticalBox::Slot().AutoHeight()
            [ SNew(STextBlock).AutoWrapText(true).Text_Lambda([this]()
                {
                    if (SearchFilter.ComponentPresence == EBlueprintTagPresence::Tagged)
                    { return LOCTEXT("TaggedScope", "Tagged targets only. Type and search filters also apply."); }
                    if (SearchFilter.ComponentPresence == EBlueprintTagPresence::Untagged)
                    { return LOCTEXT("UntaggedScope", "Keep Actor Tags; show untagged components. Other filters also apply."); }
                    return LOCTEXT("AllScope", "All tag states. Type and search filters also apply.");
                }) ]
        ]);

    const auto BlueprintPanel = Section(LOCTEXT("BlueprintHeading", "Blueprints & components"),
        LOCTEXT("BlueprintTip", "Expand a Blueprint to select its Actor Tags or components. Drag column header dividers to adjust column widths."),
        SNew(SVerticalBox)
        + SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 4)
        [
            SNew(SWrapBox).UseAllottedSize(true).InnerSlotPadding(FVector2D(5, 4))
            + SWrapBox::Slot()[ SNew(SButton).Text(LOCTEXT("All", "Check matches")).IsEnabled_Lambda([this]() { return CanEditResults(); }).OnClicked(this, &SBlueprintTagManager::CheckMatches, true) ]
            + SWrapBox::Slot()[ SNew(SButton).Text(LOCTEXT("None", "Uncheck matches")).IsEnabled_Lambda([this]() { return CanEditResults(); }).OnClicked(this, &SBlueprintTagManager::CheckMatches, false) ]
            + SWrapBox::Slot()[ SNew(SButton).Text(LOCTEXT("Refresh", "Refresh tags")).IsEnabled_Lambda([this]() { return CanEditResults(); })
                .OnClicked_Lambda([this]() { RefreshRows(); return FReply::Handled(); }) ]
            + SWrapBox::Slot()[ SNew(SButton).Text(LOCTEXT("ExpandAll", "Expand all")).OnClicked(this, &SBlueprintTagManager::ExpandGroups, true) ]
            + SWrapBox::Slot()[ SNew(SButton).Text(LOCTEXT("CollapseAll", "Collapse all")).OnClicked(this, &SBlueprintTagManager::ExpandGroups, false) ]
        ]
        + SVerticalBox::Slot().AutoHeight().Padding(0, 2, 0, 4)
        [ SNew(SSeparator) ]
        + SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 5)
        [
            SNew(SWrapBox).UseAllottedSize(true).InnerSlotPadding(FVector2D(6, 4))
            + SWrapBox::Slot().VAlign(VAlign_Center)
            [ SNew(STextBlock).Font(FBlueprintTagStyle::GetFontStyle("BoldFont")).Text(LOCTEXT("Thumbnails", "Thumbnails")) ]
            + SWrapBox::Slot()
            [ ThumbnailToggle(LOCTEXT("BlueprintThumbnails", "Blueprints"),
                LOCTEXT("BlueprintThumbnailHelp", "Show each Blueprint asset's thumbnail on its group row. Off by default; independent of component thumbnails."),
                &FBlueprintTagThumbnailOptions::bBlueprints) ]
            + SWrapBox::Slot()
            [ ThumbnailToggle(LOCTEXT("MeshThumbnails", "Static Mesh components"),
                LOCTEXT("MeshThumbnailHelp", "Show the assigned Static Mesh asset's thumbnail on component rows, including ISM and HISM. Unassigned meshes and non-StaticMesh components show No image. Off by default."),
                &FBlueprintTagThumbnailOptions::bStaticMeshComponents) ]
            + SWrapBox::Slot().VAlign(VAlign_Center).Padding(FMargin(6, 0, 0, 0))
            [ SNew(STextBlock).Text(LOCTEXT("ThumbnailSize", "Size")) ]
            + SWrapBox::Slot()[ ThumbnailSizeButton(16) ]
            + SWrapBox::Slot()[ ThumbnailSizeButton(32) ]
            + SWrapBox::Slot()[ ThumbnailSizeButton(64) ]
        ]
        + SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 4)
        [ SNew(STextBlock).AutoWrapText(true).Text(this, &SBlueprintTagManager::GetSelectionText) ]
        + SVerticalBox::Slot().FillHeight(1)
        [
            SAssignNew(OwnerTree, STreeView<TSharedPtr<FBlueprintTagOwnerRow>>).TreeItemsSource(&Groups)
            .SelectionMode(ESelectionMode::None).OnGenerateRow(this, &SBlueprintTagManager::MakeOwnerRow)
            .OnGetChildren_Lambda([](TSharedPtr<FBlueprintTagOwnerRow> Item, TArray<TSharedPtr<FBlueprintTagOwnerRow>>& Children) { Children.Append(Item->Children); })
            .OnExpansionChanged_Lambda([this](TSharedPtr<FBlueprintTagOwnerRow> Item, bool bExpanded)
                { if (bExpanded) { ExpandedAssets.Add(Item->AssetPath); } else { ExpandedAssets.Remove(Item->AssetPath); } })
            .IsEnabled_Lambda([this]() { return CanEditResults(); })
            .HeaderRow(SNew(SHeaderRow)
                + SHeaderRow::Column("Target").DefaultLabel(LOCTEXT("Target", "Blueprint / Actor / Component")).FillWidth(0.35f)
                + SHeaderRow::Column("Source").DefaultLabel(LOCTEXT("Source", "Source / Matching targets")).FillWidth(0.25f)
                + SHeaderRow::Column("Tags").DefaultLabel(LOCTEXT("Tags", "Tags / Blueprint summary")).FillWidth(0.4f))
        ]);

    const auto InventoryPanel = Section(LOCTEXT("InventoryHeading", "Tag inventory"),
        LOCTEXT("InventoryTip", "Tags across matching targets, including collapsed children. Click a tag to fill Existing tag."),
        SAssignNew(InventoryList, SListView<TSharedPtr<FBlueprintTagInventoryRow>>).ListItemsSource(&Inventory)
            .SelectionMode(ESelectionMode::Single).OnGenerateRow(this, &SBlueprintTagManager::MakeInventoryRow)
            .OnSelectionChanged_Lambda([this](TSharedPtr<FBlueprintTagInventoryRow> Item, ESelectInfo::Type)
                { if (Item && OldTagInput) { OldTagInput->SetText(FText::FromName(Item->Tag)); } }));

    const auto BatchPanel = Section(LOCTEXT("BatchHeading", "Batch edit"),
        LOCTEXT("BatchTip", "Add / clear operate on checked matching targets. Rename / remove match Existing tag within those targets."),
        SNew(SScrollBox)
        + SScrollBox::Slot()
        [
            SNew(SVerticalBox)
            + SVerticalBox::Slot().AutoHeight()
            [
                SNew(SSplitter).PhysicalSplitterHandleSize(6).HitDetectionSplitterHandleSize(10)
                + SSplitter::Slot().Value(0.5f).MinSize(230)
                [ Section(LOCTEXT("CheckedHeading", "Checked targets"),
                    LOCTEXT("CheckedTip", "These operations do not use Existing tag."),
                    SNew(SVerticalBox)
                    + SVerticalBox::Slot().AutoHeight()
                    [ InputAction(LOCTEXT("AddHeading", "Add a tag"),
                        SAssignNew(AddTagInput, SEditableTextBox).HintText(LOCTEXT("AddTag", "Tag to add (Add only)"))
                            .OnTextChanged_Lambda([this](const FText&) { InvalidatePreview(); }),
                        SNew(SButton).Text(LOCTEXT("Add", "Preview add"))
                            .IsEnabled_Lambda([this]() { return CanEditResults(); })
                            .ToolTipText(LOCTEXT("AddTip", "Add Tag to add to each checked matching target. Existing tag and Replacement tag are ignored."))
                            .OnClicked(this, &SBlueprintTagManager::MakePreview, EBlueprintTagOperation::Add)) ]
                    + SVerticalBox::Slot().AutoHeight().Padding(0, 6)
                    [ SNew(SSeparator) ]
                    + SVerticalBox::Slot().AutoHeight()
                    [ StandaloneAction(LOCTEXT("ClearHeading", "Clear all tags"),
                        LOCTEXT("ClearNoInputs", "No tag inputs are required. This action clears all tags on checked matching targets."),
                        SNew(SButton).Text(LOCTEXT("Clear", "Preview clear"))
                            .IsEnabled_Lambda([this]() { return CanEditResults(); })
                            .ToolTipText(LOCTEXT("ClearTip", "Clear every tag on each checked matching target. All tag input fields are ignored."))
                            .OnClicked(this, &SBlueprintTagManager::MakePreview, EBlueprintTagOperation::Clear)) ]) ]
                + SSplitter::Slot().Value(0.5f).MinSize(230)
                [ Section(LOCTEXT("ExistingHeading", "Match an existing tag"),
                    LOCTEXT("ExistingTip", "Only matching Existing tag values on checked matching targets are renamed or removed."),
                    SNew(SVerticalBox)
                    + SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 5)
                    [ SAssignNew(OldTagInput, SEditableTextBox).HintText(LOCTEXT("Old", "Existing tag (rename / remove)"))
                        .OnTextChanged_Lambda([this](const FText&) { InvalidatePreview(); }) ]
                    + SVerticalBox::Slot().AutoHeight()
                    [ InputAction(LOCTEXT("RenameHeading", "Rename a tag"),
                        SAssignNew(NewTagInput, SEditableTextBox).HintText(LOCTEXT("New", "Replacement tag (Rename only)"))
                            .OnTextChanged_Lambda([this](const FText&) { InvalidatePreview(); }),
                        SNew(SButton).Text(LOCTEXT("Rename", "Preview rename"))
                            .IsEnabled_Lambda([this]() { return CanEditResults(); })
                            .ToolTipText(LOCTEXT("RenameTip", "Rename Existing tag to Replacement tag within checked matching targets."))
                            .OnClicked(this, &SBlueprintTagManager::MakePreview, EBlueprintTagOperation::Rename)) ]
                    + SVerticalBox::Slot().AutoHeight().Padding(0, 6)
                    [ SNew(SSeparator) ]
                    + SVerticalBox::Slot().AutoHeight()
                    [ StandaloneAction(LOCTEXT("RemoveHeading", "Remove existing tag"),
                        LOCTEXT("RemoveInput", "Only Existing tag is used. Replacement tag and Tag to add are ignored."),
                        SNew(SButton).Text(LOCTEXT("Remove", "Preview remove"))
                            .IsEnabled_Lambda([this]() { return CanEditResults(); })
                            .ToolTipText(LOCTEXT("RemoveTip", "Remove Existing tag from checked matching targets. Other input fields are ignored."))
                            .OnClicked(this, &SBlueprintTagManager::MakePreview, EBlueprintTagOperation::Remove)) ]) ]
            ]
        ]);

    const auto PreviewPanel = Section(LOCTEXT("PreviewHeading", "Change preview"),
        LOCTEXT("PreviewTip", "Review the per-asset changes before applying. Apply is one Undo step and does not save assets."),
        SNew(SVerticalBox)
        + SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Left).Padding(0, 0, 0, 4)
        [ SNew(SButton).ButtonStyle(FBlueprintTagStyle::Get(), "PrimaryButton").Text(LOCTEXT("Apply", "Apply preview"))
            .IsEnabled_Lambda([this]() { return CanEditResults() && (PendingChanges.Num() > 0); }).OnClicked(this, &SBlueprintTagManager::ApplyPreview) ]
        + SVerticalBox::Slot().FillHeight(1)
        [
            SAssignNew(PreviewList, SListView<TSharedPtr<FString>>).ListItemsSource(&PreviewRows).SelectionMode(ESelectionMode::None)
            .OnGenerateRow_Lambda([](TSharedPtr<FString> Item, const TSharedRef<STableViewBase>& Owner)
                { return SNew(STableRow<TSharedPtr<FString>>, Owner).Padding(4)[ SNew(STextBlock).Text(FText::FromString(*Item)).AutoWrapText(true) ]; })
        ]);

    ChildSlot
    [
        SNew(SBorder).Padding(8)
        [
            SNew(SVerticalBox)
            + SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 4)
            [ SNew(STextBlock).Font(FBlueprintTagStyle::GetFontStyle("BoldFont")).Text(LOCTEXT("Title", "Project Blueprint Tag Manager")) ]
            + SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 6)
            [ SNew(STextBlock).AutoWrapText(true).Text(LOCTEXT("Help", "Click Scan project to load targets. Drag the dividers between sections or column headers to adjust their sizes.")) ]
            + SVerticalBox::Slot().FillHeight(1)
            [
                SNew(SSplitter).Orientation(Orient_Vertical).PhysicalSplitterHandleSize(6).HitDetectionSplitterHandleSize(10)
                + SSplitter::Slot().Value(0.26f).MinSize(64)
                [
                    SNew(SSplitter).PhysicalSplitterHandleSize(6).HitDetectionSplitterHandleSize(10)
                    + SSplitter::Slot().Value(0.34f).MinSize(140)[ ScanPanel ]
                    + SSplitter::Slot().Value(0.33f).MinSize(140)[ SearchPanel ]
                    + SSplitter::Slot().Value(0.33f).MinSize(140)[ FilterPanel ]
                ]
                + SSplitter::Slot().Value(0.30f).MinSize(100)[ BlueprintPanel ]
                + SSplitter::Slot().Value(0.28f).MinSize(80)
                [
                    SNew(SSplitter).PhysicalSplitterHandleSize(6).HitDetectionSplitterHandleSize(10)
                    + SSplitter::Slot().Value(0.3f).MinSize(140)[ InventoryPanel ]
                    + SSplitter::Slot().Value(0.7f).MinSize(490)[ BatchPanel ]
                ]
                + SSplitter::Slot().Value(0.16f).MinSize(70)[ PreviewPanel ]
            ]
            + SVerticalBox::Slot().AutoHeight().Padding(0, 5, 0, 4)
            [ SNew(STextBlock).AutoWrapText(true).Text_Lambda([this]() { return Status; }) ]
            + SVerticalBox::Slot().AutoHeight()
            [
                SNew(SWrapBox).UseAllottedSize(true).InnerSlotPadding(FVector2D(5, 4))
                + SWrapBox::Slot()
                [ SNew(SButton).Text(LOCTEXT("Compile", "Compile changed")).IsEnabled_Lambda([this]() { return CanEditResults() && (ChangedAssets.Num() > 0); })
                    .OnClicked(this, &SBlueprintTagManager::CompileChanged) ]
                + SWrapBox::Slot()
                [ SNew(SButton).Text(LOCTEXT("Save", "Save changed...")).IsEnabled_Lambda([this]() { return CanEditResults() && (ChangedAssets.Num() > 0); })
                    .OnClicked(this, &SBlueprintTagManager::SaveChanged) ]
                + SWrapBox::Slot().VAlign(VAlign_Center)
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
void SBlueprintTagManager::QueueRefresh() { if (bHasScanned) { bRefreshRequested = true; } }
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

FReply SBlueprintTagManager::RequestScan()
{
    if (CanWork() && !IsScanInProgress())
    {
        bScanRequested = true;
        InvalidatePreview();
    }
    return FReply::Handled();
}

void SBlueprintTagManager::BeginScan()
{
    FText Error;
    TArray<FAssetData> Assets;
    if (!FBlueprintTagProject::Discover(FolderInput->GetText().ToString(), Assets, Error)) { Status = Error; return; }
    bHasScanned = true;
    ScanChecks.Reset();
    for (const auto& Row : Rows) { ScanChecks.Add(Row->Key(), Row->bChecked); }
    Blueprints.Reset(); Rows.Reset();
    ScanAssets = MoveTemp(Assets);
    ScanIndex = 0; FailedAssets = 0; SkippedAssets = 0;
    ScanFailures.Reset(); bCanceled = false; bRefreshRequested = false;
    bScanning = (ScanAssets.Num() > 0);
    Filter();
}

void SBlueprintTagManager::ScanNext()
{
    const FAssetData& Asset = ScanAssets[ScanIndex++];
    UBlueprint* Blueprint = Cast<UBlueprint>(Asset.GetAsset());
    if (!Blueprint || (Blueprint->ParentClass && Blueprint->ParentClass->IsChildOf(AActor::StaticClass()) && !FBlueprintTagService::CanEdit(Blueprint)))
    {
        ++FailedAssets;
#if ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION == 0
        ScanFailures += Asset.ObjectPath.ToString() + TEXT("\n");
#else
        ScanFailures += Asset.GetObjectPathString() + TEXT("\n");
#endif
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

void SBlueprintTagManager::SetThumbnailOptions(const FBlueprintTagThumbnailOptions& Value)
{
    auto Options = Value;
    if (Options.Size != 16 && Options.Size != 32 && Options.Size != 64) { Options.Size = 32; }
    if (Options.bBlueprints == ThumbnailOptions.bBlueprints
        && Options.bStaticMeshComponents == ThumbnailOptions.bStaticMeshComponents && Options.Size == ThumbnailOptions.Size) { return; }
    ThumbnailOptions = Options;
    // Recreate visible row widgets without filtering or invalidating an edit preview.
    // The tree keeps the same items, check states, and expansion map.
    if (OwnerTree) { OwnerTree->RebuildList(); }
}

bool SBlueprintTagManager::ShouldShowThumbnail(const FBlueprintTagOwnerRow& Row) const
{
    return Row.bGroup ? ThumbnailOptions.bBlueprints
        : ThumbnailOptions.bStaticMeshComponents && Row.Target.Kind != EBlueprintTagTargetKind::Actor;
}

UObject* SBlueprintTagManager::ResolveThumbnailAsset(const FBlueprintTagOwnerRow& Row)
{
    if (Row.bGroup) { return Row.Blueprint.Get(); }
    if (Row.Target.Kind == EBlueprintTagTargetKind::Actor) { return nullptr; }
    // Use the effective template gathered for this Blueprint, including child
    // overrides and native defaults. ISM and HISM inherit StaticMeshComponent.
    const UStaticMeshComponent* Component = Cast<UStaticMeshComponent>(Row.Target.Object.Get());
    return Component ? Component->GetStaticMesh() : nullptr;
}

bool SBlueprintTagManager::IsGroupExpanded(const TSharedPtr<FBlueprintTagOwnerRow>& Group) const
{ return OwnerTree && OwnerTree->IsItemExpanded(Group); }

FReply SBlueprintTagManager::ToggleGroup(TSharedPtr<FBlueprintTagOwnerRow> Group)
{
    if (OwnerTree && Group && Group->bGroup) { OwnerTree->SetItemExpansion(Group, !OwnerTree->IsItemExpanded(Group)); }
    return FReply::Handled();
}

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
    if (CanWork() && !bHasScanned) { Status = LOCTEXT("NeedScan", "Choose a content folder and click Scan project to begin."); }
}

FText SBlueprintTagManager::GetScanText() const
{
    if (!bHasScanned) { return LOCTEXT("NotScanned", "Not scanned. Click Scan project to load Blueprint tags."); }
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
    if (!CanEditResults()) { return FReply::Handled(); }
    FName OldTag, NewTag;
    if ((Operation == EBlueprintTagOperation::Rename || Operation == EBlueprintTagOperation::Remove)
        && !FBlueprintTagService::ParseTag(OldTagInput->GetText().ToString(), OldTag, Status)) { return FReply::Handled(); }
    if (Operation == EBlueprintTagOperation::Add
        && !FBlueprintTagService::ParseTag(AddTagInput->GetText().ToString(), NewTag, Status)) { return FReply::Handled(); }
    if (Operation == EBlueprintTagOperation::Rename
        && !FBlueprintTagService::ParseTag(NewTagInput->GetText().ToString(), NewTag, Status)) { return FReply::Handled(); }
    TMap<UBlueprint*, TSet<FString>> Selected;
    for (const auto& Row : MatchingRows)
    { if (Row->bChecked && Row->Target.bEditable && Row->Blueprint.IsValid()) { Selected.FindOrAdd(Row->Blueprint.Get()).Add(Row->Target.Id); } }
    for (const auto& Blueprint : Blueprints)
    {
        const TSet<FString>* Ids = Selected.Find(Blueprint.Get());
        if (!Ids) { continue; }
        auto Changes = FBlueprintTagService::Preview(Blueprint.Get(), *Ids, Operation, OldTag, NewTag);
        if ((Changes.Num() == 0)) { continue; }
        for (const auto& Change : Changes)
        {
            PreviewRows.Add(MakeShared<FString>(Blueprint->GetPathName() + TEXT(" : ") + Change.Label + TEXT("\n  ")
                + FBlueprintTagService::FormatTags(Change.Before) + TEXT("  ->  ") + FBlueprintTagService::FormatTags(Change.After)));
        }
        PendingChanges.Add({ Blueprint.Get(), MoveTemp(Changes) });
    }
    PreviewList->RequestListRefresh();
    Status = (PendingChanges.Num() == 0) ? LOCTEXT("NoChange", "No changes. Check matching targets and tag names; matching is case-insensitive.")
        : FText::Format(LOCTEXT("Ready", "Preview: {0} targets in {1} Blueprints. Apply is one Undo step; changes are not saved automatically."), PreviewRows.Num(), PendingChanges.Num());
    return FReply::Handled();
}

FReply SBlueprintTagManager::ApplyPreview()
{
    FText Error;
    const bool bSuccess = CanEditResults() && FBlueprintTagService::ApplyBatch(PendingChanges, Error);
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
    if ((Packages.Num() == 0)) { Status = LOCTEXT("NothingToSave", "No tracked assets need saving."); return FReply::Handled(); }
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
    const bool bShowThumbnail = ShouldShowThumbnail(*Item);
    UObject* ThumbnailAsset = bShowThumbnail ? ResolveThumbnailAsset(*Item) : nullptr;
    // No asset thumbnail objects or render pool are created while both switches
    // are off. The virtualized tree only requests thumbnails for visible rows.
    if (ThumbnailAsset && !ThumbnailPool) { ThumbnailPool = MakeShared<FAssetThumbnailPool>(128); }
    class SOwnerRow : public SMultiColumnTableRow<TSharedPtr<FBlueprintTagOwnerRow>>
    {
    public:
        SLATE_BEGIN_ARGS(SOwnerRow) : _ShowThumbnail(false), _ThumbnailSize(32), _ThumbnailAsset(nullptr) {}
            SLATE_ARGUMENT(bool, ShowThumbnail)
            SLATE_ARGUMENT(int32, ThumbnailSize)
            SLATE_ARGUMENT(UObject*, ThumbnailAsset)
            SLATE_ARGUMENT(TSharedPtr<FAssetThumbnailPool>, ThumbnailPool)
        SLATE_END_ARGS()
        TSharedPtr<FBlueprintTagOwnerRow> Item;
        FSimpleDelegate Changed;
        FOnClicked Open;
        FOnClicked Toggle;
        TSharedPtr<FAssetThumbnail> Thumbnail;
        bool bShowThumbnail = false;
        int32 ThumbnailSize = 32;
        void Construct(const FArguments& Args, const TSharedRef<STableViewBase>& Table,
            TSharedPtr<FBlueprintTagOwnerRow> InItem, FSimpleDelegate InChanged, FOnClicked InOpen, FOnClicked InToggle)
        {
            Item = InItem; Changed = InChanged; Open = InOpen; Toggle = InToggle;
            bShowThumbnail = Args._ShowThumbnail; ThumbnailSize = Args._ThumbnailSize;
            if (Args._ThumbnailAsset) { Thumbnail = MakeShared<FAssetThumbnail>(Args._ThumbnailAsset, ThumbnailSize, ThumbnailSize, Args._ThumbnailPool); }
            SMultiColumnTableRow::Construct(FSuperRowType::FArguments().Padding(4), Table);
        }
        virtual TSharedRef<SWidget> GenerateWidgetForColumn(const FName& Column) override
        {
            const bool bActor = !Item->bGroup && Item->Target.Kind == EBlueprintTagTargetKind::Actor;
            const FSlateFontInfo Font = FBlueprintTagStyle::GetFontStyle(bActor ? "BoldFont" : "NormalFont");
            if (Column == "Target")
            {
                TSharedRef<SWidget> Preview = SNullWidget::NullWidget;
                if (bShowThumbnail)
                {
                    if (Thumbnail)
                    {
                        FAssetThumbnailConfig Config;
                        Config.bAllowHintText = false;
                        Config.bAllowRealTimeOnHovered = false;
                        Config.ThumbnailLabel = EThumbnailLabel::NoLabel;
                        Preview = SNew(SBox).WidthOverride(ThumbnailSize).HeightOverride(ThumbnailSize)
                            .ToolTipText(FText::FromString(Thumbnail->GetAssetData().GetFullName()))
                            [ Thumbnail->MakeThumbnailWidget(Config) ];
                    }
                    else
                    {
                        Preview = SNew(SBox).MinDesiredWidth(ThumbnailSize).HeightOverride(ThumbnailSize).VAlign(VAlign_Center)
                            .ToolTipText(Item->bGroup ? LOCTEXT("NoBlueprintImage", "Blueprint asset is unavailable.")
                                : Item->Target.bStaticMeshComponent ? LOCTEXT("NoMeshImage", "No Static Mesh asset is assigned.")
                                    : LOCTEXT("NotMeshImage", "This component is not a StaticMeshComponent."))
                            [ SNew(STextBlock).Text(LOCTEXT("NoImage", "No image"))
                                .ColorAndOpacity(FSlateColor::UseSubduedForeground()) ];
                    }
                }
                const FString Label = Item->bGroup ? FPackageName::ObjectPathToObjectName(Item->AssetPath)
                    : Item->Target.Kind == EBlueprintTagTargetKind::Actor ? TEXT("Actor Tags") : Item->Target.Label;
                TSharedRef<SWidget> Name = SNew(STextBlock).Text(FText::FromString(Label))
                    .Font(Font)
                    .ToolTipText(FText::FromString(Item->bGroup ? Item->AssetPath : Label))
#if ENGINE_MAJOR_VERSION > 5 || ENGINE_MINOR_VERSION >= 1
                    .OverflowPolicy(ETextOverflowPolicy::Ellipsis)
#endif
                    ;
                if (Item->bGroup)
                {
                    Name = SNew(SHorizontalBox)
                        + SHorizontalBox::Slot().FillWidth(1).VAlign(VAlign_Center)
                        [ SNew(SButton).ButtonStyle(FBlueprintTagStyle::Get(), "SimpleButton").OnClicked(Toggle)
                            .ToolTipText(FText::FromString(Item->AssetPath + TEXT("\nClick to expand / collapse")))[ Name ] ]
                        + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(5, 0, 0, 0)
                        [ SNew(SButton).Text(LOCTEXT("OpenBlueprint", "Open Blueprint")).OnClicked(Open)
                            .ToolTipText(FText::FromString(Item->AssetPath)) ];
                }
                return SNew(SHorizontalBox)
                    + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)[ SNew(SExpanderArrow, SharedThis(this)) ]
                    + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0, 0, 5, 0)
                    [ SNew(SCheckBox).IsEnabled(Item->CanCheck())
                        .ToolTipText(Item->bGroup ? LOCTEXT("GroupCheck", "Select matching Actor / component targets in this Blueprint, including collapsed children.")
                            : LOCTEXT("TargetCheck", "Select this target for batch editing."))
                        .IsChecked_Lambda([this]() { return Item->CheckState(); })
                        .OnCheckStateChanged_Lambda([this](ECheckBoxState State) { Item->SetChecked(State == ECheckBoxState::Checked); Changed.ExecuteIfBound(); }) ]
                    + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0, 0, bShowThumbnail ? 6 : 0, 0)[ Preview ]
                    + SHorizontalBox::Slot().FillWidth(1).VAlign(VAlign_Center)[ Name ];
            }
            const FString Text = Item->bGroup
                ? (Column == "Source" ? FString::Printf(TEXT("%d / %d components shown\n%s"), Item->MatchingComponents,
                    Item->TotalComponents, Item->AllTargetsHaveTags() ? TEXT("All targets have tags")
                        : Item->bActorHasTags ? TEXT("Actor tags: Present") : TEXT("Actor tags: None")) : Item->Summary)
                : (Column == "Source" ? (bActor ? FString(TEXT("Actor defaults")) : Item->Target.Source) + (Item->Target.bEditable ? TEXT("") : TEXT(" (read only)"))
                    : FBlueprintTagService::FormatTags(Item->Target.Tags));
            return SNew(STextBlock).Text(FText::FromString(Text)).Font(Font)
                .ToolTipText(FText::FromString(Text))
#if ENGINE_MAJOR_VERSION > 5 || ENGINE_MINOR_VERSION >= 1
                .OverflowPolicy(ETextOverflowPolicy::Ellipsis)
#endif
                ;
        }
    };
    return SNew(SOwnerRow, Owner, Item, FSimpleDelegate::CreateSP(this, &SBlueprintTagManager::InvalidatePreview),
        FOnClicked::CreateSP(this, &SBlueprintTagManager::OpenBlueprint, Item), FOnClicked::CreateSP(this, &SBlueprintTagManager::ToggleGroup, Item))
        .ShowThumbnail(bShowThumbnail).ThumbnailSize(ThumbnailOptions.Size).ThumbnailAsset(ThumbnailAsset).ThumbnailPool(ThumbnailPool);
}

TSharedRef<ITableRow> SBlueprintTagManager::MakeInventoryRow(TSharedPtr<FBlueprintTagInventoryRow> Item, const TSharedRef<STableViewBase>& Owner)
{
    const FText Text = FText::FromString(FString::Printf(TEXT("%s  |  %d uses in %d targets"), *Item->Tag.ToString(), Item->Uses, Item->OwnersCount));
    return SNew(STableRow<TSharedPtr<FBlueprintTagInventoryRow>>, Owner).Padding(4)
        [ SNew(STextBlock).Text(Text).ToolTipText(FText::FromString(Item->Owners)) ];
}

#undef LOCTEXT_NAMESPACE
