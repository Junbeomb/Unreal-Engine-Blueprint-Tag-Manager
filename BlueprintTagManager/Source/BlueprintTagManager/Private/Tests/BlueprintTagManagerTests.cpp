#include "BlueprintTagService.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "BlueprintEditor.h"
#include "BlueprintEditorModule.h"
#include "BlueprintTagProject.h"
#include "SBlueprintTagManager.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "AssetRegistry/IAssetRegistry.h"
#include "Components/BoxComponent.h"
#include "Components/HierarchicalInstancedStaticMeshComponent.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Editor.h"
#include "Engine/Blueprint.h"
#include "Engine/BlueprintGeneratedClass.h"
#include "Engine/InheritableComponentHandler.h"
#include "Engine/SCS_Node.h"
#include "Engine/SimpleConstructionScript.h"
#include "Engine/StaticMeshActor.h"
#include "Engine/StaticMesh.h"
#include "GameFramework/Actor.h"
#include "Framework/Docking/TabManager.h"
#include "Framework/Application/SlateApplication.h"
#include "ImageUtils.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "Misc/AutomationTest.h"
#include "Misc/CommandLine.h"
#include "Misc/ConfigCacheIni.h"
#include "Misc/FileHelper.h"
#include "Misc/Parse.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"
#include "HAL/FileManager.h"
#include "PackageTools.h"
#include "UObject/Package.h"
#include "UObject/SavePackage.h"
#include "Widgets/Docking/SDockTab.h"
#include "Widgets/SWindow.h"
#include "Widgets/Input/SEditableTextBox.h"

namespace BlueprintTagManagerTests
{
    UBlueprint* MakeBlueprint(UClass* Parent)
    {
        const FString Name = TEXT("/Temp/BTM_") + FGuid::NewGuid().ToString(EGuidFormats::Digits);
        UPackage* Package = CreatePackage(*Name);
        return FKismetEditorUtilities::CreateBlueprint(Parent, Package, FName(*FPackageName::GetShortName(Name)),
            BPTYPE_Normal, UBlueprint::StaticClass(), UBlueprintGeneratedClass::StaticClass());
    }

    TSet<FString> AllEditable(UBlueprint* Blueprint)
    {
        TSet<FString> Result;
        for (const auto& Target : FBlueprintTagService::Gather(Blueprint))
        {
            if (Target.bEditable) { Result.Add(Target.Id); }
        }
        return Result;
    }
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBlueprintTagOperationsTest, "BlueprintTagManager.TagOperations",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FBlueprintTagOperationsTest::RunTest(const FString& Parameters)
{
    using Op = EBlueprintTagOperation;
    const TArray<FName> Original = { TEXT("Enemy"), TEXT("Enemy"), TEXT("Interactable"), TEXT("Keep"), TEXT("Keep") };
    TestEqual(TEXT("Add does not duplicate"), FBlueprintTagService::Transform(Original, Op::Add, NAME_None, TEXT("enemy")).Num(), 5);
    TestEqual(TEXT("Add new tag"), FBlueprintTagService::Transform(Original, Op::Add, NAME_None, TEXT("New")).Num(), 6);
    const TArray<FName> Renamed = FBlueprintTagService::Transform(Original, Op::Rename, TEXT("enemy"), TEXT("Interactable"));
    TestEqual(TEXT("Rename merges collisions only"), Renamed.Num(), 3);
    TestTrue(TEXT("Unrelated duplicates remain"), Renamed == TArray<FName>({ TEXT("Interactable"), TEXT("Keep"), TEXT("Keep") }));
    TestTrue(TEXT("Missing rename is a no-op"), FBlueprintTagService::Transform(Original, Op::Rename, TEXT("Missing"), TEXT("Keep")) == Original);
    TestEqual(TEXT("Remove every occurrence"), FBlueprintTagService::Transform(Original, Op::Remove, TEXT("Enemy")).Num(), 3);
    TestEqual(TEXT("Clear all"), FBlueprintTagService::Transform(Original, Op::Clear).Num(), 0);
    TestTrue(TEXT("Add ignores Existing tag"), FBlueprintTagService::Transform(Original, Op::Add, TEXT("Missing"), TEXT("New"))
        == FBlueprintTagService::Transform(Original, Op::Add, NAME_None, TEXT("New")));
    TestEqual(TEXT("Clear ignores both tag inputs"), FBlueprintTagService::Transform(Original, Op::Clear, TEXT("Missing"), TEXT("New")).Num(), 0);
    FName Parsed;
    FText Error;
    TestFalse(TEXT("Empty rejected"), FBlueprintTagService::ParseTag(TEXT("  "), Parsed, Error));
    TestFalse(TEXT("None rejected"), FBlueprintTagService::ParseTag(TEXT("None"), Parsed, Error));
    TestFalse(TEXT("Line breaks rejected"), FBlueprintTagService::ParseTag(TEXT("A\nB"), Parsed, Error));
    TestFalse(TEXT("Overlong FName rejected"), FBlueprintTagService::ParseTag(FString::ChrN(NAME_SIZE, TCHAR('A')), Parsed, Error));
    TestTrue(TEXT("Trim input"), FBlueprintTagService::ParseTag(TEXT(" Enemy "), Parsed, Error) && Parsed == TEXT("Enemy"));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBlueprintTagInheritanceTest, "BlueprintTagManager.BlueprintInheritance",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FBlueprintTagInheritanceTest::RunTest(const FString& Parameters)
{
    using namespace BlueprintTagManagerTests;
    UBlueprint* Parent = MakeBlueprint(AActor::StaticClass());
    if (!TestNotNull(TEXT("Parent Blueprint"), Parent)) { return false; }
    USCS_Node* Box = Parent->SimpleConstructionScript->CreateNode(UBoxComponent::StaticClass(), TEXT("TaggedBox"));
    Box->ComponentTemplate->ComponentTags = { TEXT("Shared"), TEXT("BoxOnly") };
    Parent->SimpleConstructionScript->AddNode(Box);
    USCS_Node* Mesh = Parent->SimpleConstructionScript->CreateNode(UStaticMeshComponent::StaticClass(), TEXT("UntaggedMesh"));
    Box->AddChildNode(Mesh);
    CastChecked<AActor>(Parent->GeneratedClass->GetDefaultObject())->Tags = { TEXT("Shared"), TEXT("ActorOnly") };
    FKismetEditorUtilities::CompileBlueprint(Parent);
    UBlueprint* Child = MakeBlueprint(Parent->GeneratedClass);
    if (!TestNotNull(TEXT("Child Blueprint"), Child)) { return false; }
    const TArray<FBlueprintTagTarget> Initial = FBlueprintTagService::Gather(Child);
    TestTrue(TEXT("Includes actor and inherited components, including untagged ones"), Initial.Num() >= 3);
    TestNull(TEXT("Reading does not create override handler"), Child->GetInheritableComponentHandler(false));
    const auto* BoxTarget = Initial.FindByPredicate([](const auto& Item) { return Item.Label == TEXT("TaggedBox"); });
    if (!TestNotNull(TEXT("Inherited TaggedBox found"), BoxTarget)) { return false; }
    TestTrue(TEXT("Marked inherited and editable"), BoxTarget->Kind == EBlueprintTagTargetKind::InheritedComponent && BoxTarget->bEditable);

    FText Error;
    const auto Plan = FBlueprintTagService::Preview(Child, AllEditable(Child), EBlueprintTagOperation::Rename, TEXT("Shared"), TEXT("Renamed"));
    TestEqual(TEXT("Rename affects actor and tagged component only"), Plan.Num(), 2);
    TestTrue(TEXT("Apply inherited rename"), FBlueprintTagService::Apply(Child, Plan, Error));
    TestTrue(TEXT("Parent actor remains unchanged"), CastChecked<AActor>(Parent->GeneratedClass->GetDefaultObject())->Tags.Contains(TEXT("Shared")));
    TestTrue(TEXT("Parent template remains unchanged"), Box->ComponentTemplate->ComponentTags.Contains(TEXT("Shared")));
    auto* ChildClass = CastChecked<UBlueprintGeneratedClass>(Child->GeneratedClass);
    TestTrue(TEXT("Child has overridden component tag"), Box->GetActualComponentTemplate(ChildClass)->ComponentTags.Contains(TEXT("Renamed")));
    TestTrue(TEXT("Child actor tag changed"), CastChecked<AActor>(ChildClass->GetDefaultObject())->Tags.Contains(TEXT("Renamed")));

    GEditor->UndoTransaction();
    ChildClass = CastChecked<UBlueprintGeneratedClass>(Child->GeneratedClass);
    TestTrue(TEXT("Undo actor tags"), CastChecked<AActor>(ChildClass->GetDefaultObject())->Tags.Contains(TEXT("Shared")));
    TestTrue(TEXT("Undo inherited override"), Box->GetActualComponentTemplate(ChildClass)->ComponentTags.Contains(TEXT("Shared")));
    GEditor->RedoTransaction();
    ChildClass = CastChecked<UBlueprintGeneratedClass>(Child->GeneratedClass);
    TestTrue(TEXT("Redo actor tags"), CastChecked<AActor>(ChildClass->GetDefaultObject())->Tags.Contains(TEXT("Renamed")));
    TestTrue(TEXT("Redo inherited override"), Box->GetActualComponentTemplate(ChildClass)->ComponentTags.Contains(TEXT("Renamed")));
    FKismetEditorUtilities::CompileBlueprint(Child);
    ChildClass = CastChecked<UBlueprintGeneratedClass>(Child->GeneratedClass);
    TestTrue(TEXT("Compile preserves component tags"), Box->GetActualComponentTemplate(ChildClass)->ComponentTags.Contains(TEXT("Renamed")));
    TestTrue(TEXT("Compile preserves actor tags"), CastChecked<AActor>(ChildClass->GetDefaultObject())->Tags.Contains(TEXT("Renamed")));

    // A stale preview must reject the whole batch, including otherwise valid targets.
    const auto Stale = FBlueprintTagService::Preview(Child, AllEditable(Child), EBlueprintTagOperation::Add, NAME_None, TEXT("Stale"));
    AActor* CDO = CastChecked<AActor>(ChildClass->GetDefaultObject());
    CDO->Tags.Add(TEXT("ExternalEdit"));
    TestFalse(TEXT("Stale preview rejected"), FBlueprintTagService::Apply(Child, Stale, Error));
    TestFalse(TEXT("Rejected batch has no partial writes"), Box->GetActualComponentTemplate(ChildClass)->ComponentTags.Contains(TEXT("Stale")));

    TSet<FString> ActorOnly = { TEXT("Actor") };
    auto ActorPlan = FBlueprintTagService::Preview(Child, ActorOnly, EBlueprintTagOperation::Clear, NAME_None, NAME_None);
    TestEqual(TEXT("Checkbox scope"), ActorPlan.Num(), 1);
    TestTrue(TEXT("Clear scoped actor tags"), FBlueprintTagService::Apply(Child, ActorPlan, Error));
    TestTrue(TEXT("Component tags retained"), Box->GetActualComponentTemplate(ChildClass)->ComponentTags.Contains(TEXT("Renamed")));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBlueprintTagNativeTest, "BlueprintTagManager.NativeComponents",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FBlueprintTagNativeTest::RunTest(const FString& Parameters)
{
    using namespace BlueprintTagManagerTests;
    UBlueprint* Blueprint = MakeBlueprint(AStaticMeshActor::StaticClass());
    if (!TestNotNull(TEXT("Native derived Blueprint"), Blueprint)) { return false; }
    TSet<FString> Components;
    for (const auto& Target : FBlueprintTagService::Gather(Blueprint))
    {
        if (Target.Kind == EBlueprintTagTargetKind::NativeComponent && Target.bEditable) { Components.Add(Target.Id); }
    }
    TestTrue(TEXT("Native components found"), Components.Num() > 0);
    FText Error;
    const auto Plan = FBlueprintTagService::Preview(Blueprint, Components, EBlueprintTagOperation::Add, NAME_None, TEXT("NativeTag"));
    TestTrue(TEXT("Apply native tag"), FBlueprintTagService::Apply(Blueprint, Plan, Error));
    TestFalse(TEXT("Base C++ CDO not changed"), GetDefault<AStaticMeshActor>()->GetStaticMeshComponent()->ComponentTags.Contains(TEXT("NativeTag")));
    FKismetEditorUtilities::CompileBlueprint(Blueprint);
    auto* CDO = CastChecked<AStaticMeshActor>(Blueprint->GeneratedClass->GetDefaultObject());
    TestTrue(TEXT("Native component tag survives compile"), CDO->GetStaticMeshComponent()->ComponentTags.Contains(TEXT("NativeTag")));
    TestFalse(TEXT("Actor tags excluded from scope"), CDO->Tags.Contains(TEXT("NativeTag")));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBlueprintTagEditorTest, "BlueprintTagManager.EditorIntegration",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FBlueprintTagEditorTest::RunTest(const FString& Parameters)
{
    const FString Name = TEXT("/Game/BTM_UI_") + FGuid::NewGuid().ToString(EGuidFormats::Digits);
    UPackage* Package = CreatePackage(*Name);
    UBlueprint* Blueprint = FKismetEditorUtilities::CreateBlueprint(AActor::StaticClass(), Package,
        FName(*FPackageName::GetShortName(Name)), BPTYPE_Normal, UBlueprint::StaticClass(), UBlueprintGeneratedClass::StaticClass());
    if (!TestNotNull(TEXT("Project Blueprint"), Blueprint)) { return false; }
    USCS_Node* Box = Blueprint->SimpleConstructionScript->CreateNode(UBoxComponent::StaticClass(), TEXT("UISensor"));
    Box->ComponentTemplate->ComponentTags = { TEXT("UIComponentTag") };
    Blueprint->SimpleConstructionScript->AddNode(Box);
    USCS_Node* Mesh = Blueprint->SimpleConstructionScript->CreateNode(UStaticMeshComponent::StaticClass(), TEXT("UIEmptyMesh"));
    Box->AddChildNode(Mesh);
    CastChecked<AActor>(Blueprint->GeneratedClass->GetDefaultObject())->Tags = { TEXT("UIActorTag") };
    FKismetEditorUtilities::CompileBlueprint(Blueprint);
    FAssetRegistryModule::AssetCreated(Blueprint);
    auto& Kismet = FModuleManager::LoadModuleChecked<FBlueprintEditorModule>("Kismet");
    const int32 EditorsBefore = Kismet.GetBlueprintEditors().Num();
    const auto Tab = FGlobalTabmanager::Get()->TryInvokeTab(FName(TEXT("BlueprintTagManager.Project")));
    TestTrue(TEXT("Project Tag Manager opens from the main editor tab manager"), Tab.IsValid());
    if (Tab)
    {
        const auto Widget = StaticCastSharedRef<SBlueprintTagManager>(Tab->GetContent());
        Widget->SlatePrepass();
        auto CaptureUI = [this, &Widget](const TCHAR* Filename)
        {
            if (!FParse::Param(FCommandLine::Get(), TEXT("BTMCaptureUI"))) { return; }
            if (const auto Window = FSlateApplication::Get().FindWidgetWindow(Widget)) { Window->Resize(FVector2D(1280, 960)); }
            FSlateApplication::Get().Tick();
            Widget->SlatePrepass();
            TArray<FColor> Pixels;
            FIntVector Size;
            if (!TestTrue(TEXT("Capture rendered panel"), FSlateApplication::Get().TakeScreenshot(Widget, Pixels, Size))) { return; }
            TArray64<uint8> PNG;
            FImageUtils::PNGCompressImageArray(Size.X, Size.Y, Pixels, PNG);
            TestTrue(TEXT("Save panel capture"), FFileHelper::SaveArrayToFile(PNG, *FPaths::Combine(FPaths::ProjectSavedDir(), Filename)));
        };
        TestFalse(TEXT("Opening the panel does not request a scan"), Widget->IsScanInProgress());
        for (int32 Frame = 0; Frame < 32; ++Frame) { Widget->Tick(FGeometry(), FPlatformTime::Seconds(), 0.016f); }
        TestFalse(TEXT("Idle ticks do not start a project scan"), Widget->IsScanInProgress());
        TestEqual(TEXT("No Blueprints loaded before Scan project is clicked"), Widget->GetBlueprintCount(), 0);
        TestEqual(TEXT("No targets collected before Scan project is clicked"), Widget->GetTargetCount(), 0);
        CaptureUI(TEXT("BlueprintTagManager-Idle.png"));
        Widget->RequestScan();
        TestTrue(TEXT("Scan project action requests a scan"), Widget->IsScanInProgress());
        for (int32 Frame = 0; Frame < 200 && Widget->IsScanInProgress(); ++Frame)
        {
            Widget->Tick(FGeometry(), FPlatformTime::Seconds(), 0.016f);
        }
        TestFalse(TEXT("Project scan completes"), Widget->IsScanInProgress());
        TestTrue(TEXT("Project scan finds a Blueprint without opening it"), Widget->GetBlueprintCount() >= 1);
        TestTrue(TEXT("Project view includes actor and component targets"), Widget->GetTargetCount() >= 1);
        TestEqual(TEXT("Scanning does not open any Blueprint editors"), Kismet.GetBlueprintEditors().Num(), EditorsBefore);
        FBlueprintTagFilter Filter;
        Filter.Blueprint = Name;
        Widget->SetFilter(Filter);
        TestTrue(TEXT("StaticMesh-only filter defaults on"), Filter.bStaticMeshOnly);
        if (Widget->GetGroups().Num() == 1)
        {
            const auto Group = Widget->GetGroups()[0];
            TestEqual(TEXT("Default view has separate actor plus StaticMesh component, not box"), Group->Children.Num(), 2);
            TestEqual(TEXT("Default type filter shows one of two components"), Group->MatchingComponents, 1);
            TestTrue(TEXT("Overview omits tags on hidden non-mesh components"), Group->Summary.EndsWith(TEXT("Components (shown): []")));
            Widget->ToggleGroup(Group);
            TestTrue(TEXT("Blueprint name action expands group"), Widget->IsGroupExpanded(Group));
            Widget->ToggleGroup(Group);
            TestFalse(TEXT("Blueprint name action collapses group"), Widget->IsGroupExpanded(Group));
            TestEqual(TEXT("Name action does not open a Blueprint editor"), Kismet.GetBlueprintEditors().Num(), EditorsBefore);
            Filter.bExcludeHISM = true; Filter.bExcludeISM = true;
            Widget->SetFilter(Filter);
            CaptureUI(TEXT("BlueprintTagManager-Exclusions.png"));
        }
        Filter.bExcludeHISM = false; Filter.bExcludeISM = false;
        Filter.bStaticMeshOnly = false;
        Widget->SetFilter(Filter);
        TestEqual(TEXT("One Blueprint group instead of flat targets"), Widget->GetGroups().Num(), 1);
        if (Widget->GetGroups().Num() == 1)
        {
            auto Group = Widget->GetGroups()[0];
            TestFalse(TEXT("Blueprint starts collapsed"), Widget->IsGroupExpanded(Group));
            TestEqual(TEXT("Actor and two components are children"), Group->Children.Num(), 3);
            TestTrue(TEXT("Overview summarizes actor and component tags"), Group->Summary.Contains(TEXT("UIActorTag")) && Group->Summary.Contains(TEXT("UIComponentTag")));
            Group->SetChecked(true);
            Widget->ExpandGroups(true);
            TestTrue(TEXT("Blueprint can expand"), Widget->IsGroupExpanded(Group));
            CaptureUI(TEXT("BlueprintTagManager-Expanded.png"));
            Widget->ExpandGroups(false);
            TestFalse(TEXT("Blueprint can collapse again"), Widget->IsGroupExpanded(Group));
            TSet<FString> Selected;
            for (const auto& Row : Widget->GetMatchingTargets()) { if (Row->bChecked) { Selected.Add(Row->Target.Id); } }
            TestEqual(TEXT("Collapsing preserves batch scope"), Selected.Num(), 3);
            TestEqual(TEXT("Collapsed group preview includes actor and tagged component"),
                FBlueprintTagService::Preview(Blueprint, Selected, EBlueprintTagOperation::Clear, NAME_None, NAME_None).Num(), 2);
        }
        Filter.ComponentPresence = EBlueprintTagPresence::Tagged;
        Widget->SetFilter(Filter);
        TestEqual(TEXT("Tagged filter keeps actor and tagged component"), Widget->GetMatchingTargets().Num(), 2);
        Filter.ComponentPresence = EBlueprintTagPresence::Untagged;
        Filter.Component = TEXT("UIEmpty");
        Widget->SetFilter(Filter);
        TestEqual(TEXT("Independent component search with untagged filter"), Widget->GetMatchingTargets().Num(), 1);
        Filter.Tags = TEXT("UIActorTag");
        Widget->SetFilter(Filter);
        TestEqual(TEXT("Component and tag fields combine, not cross-match"), Widget->GetGroups().Num(), 0);
        Tab->RequestCloseTab();
    }
    FAssetRegistryModule::AssetDeleted(Blueprint);
    Package->SetDirtyFlag(false);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBlueprintTagViewTest, "BlueprintTagManager.ViewFiltering",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FBlueprintTagViewTest::RunTest(const FString& Parameters)
{
    TArray<TSharedPtr<FBlueprintTagOwnerRow>> Rows, Roots, Matches;
    auto Add = [&Rows](const TCHAR* Path, const TCHAR* Label, const TCHAR* Source, EBlueprintTagTargetKind Kind, TArray<FName> Tags)
    {
        auto Row = MakeShared<FBlueprintTagOwnerRow>();
        Row->AssetPath = Path; Row->Target.Id = Label; Row->Target.Label = Label;
        Row->Target.Source = Source; Row->Target.Kind = Kind; Row->Target.Tags = MoveTemp(Tags);
        Row->Target.bEditable = true; Rows.Add(Row);
    };
    Add(TEXT("/Game/Hero.Hero"), TEXT("HeroActor"), TEXT("Actor Tags"), EBlueprintTagTargetKind::Actor, { TEXT("ActorOnly") });
    Add(TEXT("/Game/Hero.Hero"), TEXT("Sensor"), TEXT("SCS"), EBlueprintTagTargetKind::Component, { TEXT("Enemy"), TEXT("Enemy"), TEXT("SensorOnly") });
    Add(TEXT("/Game/Hero.Hero"), TEXT("Enemy"), TEXT("Native"), EBlueprintTagTargetKind::NativeComponent, {});
    Add(TEXT("/Game/Enemy.Enemy"), TEXT("Other"), TEXT("EnemyOrigin"), EBlueprintTagTargetKind::Component, { TEXT("Hero") });
    FBlueprintTagFilter Filter;
    // Exercise text/tag filters across all component types; type filtering has its own test.
    auto Build = [&]() { Filter.bStaticMeshOnly = false; FBlueprintTagView::Build(Rows, Filter, Roots, Matches); };
    Build();
    TestEqual(TEXT("One root per Blueprint"), Roots.Num(), 2);
    TestEqual(TEXT("All targets match by default"), Matches.Num(), 4);
    TestEqual(TEXT("Component count excludes actor"), Roots[0]->TotalComponents, 2);
    Rows[2]->Target.bEditable = false;
    Roots[0]->SetChecked(true);
    TestTrue(TEXT("Group selection reaches editable children"), Rows[0]->bChecked && Rows[1]->bChecked);
    TestFalse(TEXT("Read-only children cannot be checked"), Rows[2]->bChecked);
    TestTrue(TEXT("Read-only targets excluded from group check state"), Roots[0]->CheckState() == ECheckBoxState::Checked);
    Rows[0]->SetChecked(false);
    TestTrue(TEXT("Partial group selection is indeterminate"), Roots[0]->CheckState() == ECheckBoxState::Undetermined);
    Rows[0]->SetChecked(true);
    Filter.Tags = TEXT("Enemy"); Build();
    TestEqual(TEXT("Tag search excludes matching component names, paths and sources"), Matches.Num(), 1);
    TestTrue(TEXT("Tag search finds actual tag"), Matches.Num() == 1 && Matches[0] == Rows[1]);
    TestTrue(TEXT("Tag search summarizes all tags of the matching component, without duplicates"),
        Roots[0]->Summary.EndsWith(TEXT("Components (shown): [Enemy, SensorOnly]")));
    Roots[0]->SetChecked(false);
    TestFalse(TEXT("Filtered group selection changes matching target"), Rows[1]->bChecked);
    TestTrue(TEXT("Filtered group selection preserves excluded target"), Rows[0]->bChecked);
    Filter = {}; Filter.Blueprint = TEXT("Enemy"); Build();
    TestTrue(TEXT("Blueprint search ignores tags and component names"), Matches.Num() == 1 && Matches[0] == Rows[3]);
    Filter = {}; Filter.Component = TEXT("Enemy"); Build();
    TestTrue(TEXT("Component search ignores Blueprint, source and tag values"), Matches.Num() == 1 && Matches[0] == Rows[2]);
    TestTrue(TEXT("Component search excludes hidden component tags from the summary"),
        Roots[0]->Summary.EndsWith(TEXT("Components (shown): []")));
    Filter = {}; Filter.Blueprint = TEXT("game hero"); Filter.Tags = TEXT("ENE"); Build();
    TestTrue(TEXT("Fields combine with case-insensitive AND terms"), Matches.Num() == 1 && Matches[0] == Rows[1]);
    Filter.Component = TEXT("Enemy"); Build();
    TestEqual(TEXT("Terms cannot cross fields"), Matches.Num(), 0);
    Filter = {}; Filter.ComponentPresence = EBlueprintTagPresence::Tagged; Build();
    TestEqual(TEXT("Tagged actors and components"), Matches.Num(), 3);
    Filter.ComponentPresence = EBlueprintTagPresence::Untagged; Build();
    TestTrue(TEXT("Untagged components plus actor"), Matches.Num() == 2 && Matches.Contains(Rows[0]) && Matches.Contains(Rows[2]));
    TestEqual(TEXT("Summary keeps total component count"), Roots[0]->TotalComponents, 2);
    TestEqual(TEXT("Summary reports matching component count"), Roots[0]->MatchingComponents, 1);
    TestTrue(TEXT("Without tags does not leak tagged component summaries"),
        Roots[0]->Summary.EndsWith(TEXT("Components (shown): []")));
    Filter.Tags = TEXT("none"); Build();
    TestEqual(TEXT("Empty-tag display text is not a tag"), Matches.Num(), 0);

    Rows.Reset();
    Add(TEXT("/Game/Empty.Empty"), TEXT("Actor"), TEXT("Actor Tags"), EBlueprintTagTargetKind::Actor, {});
    Add(TEXT("/Game/Empty.Empty"), TEXT("EmptyBox"), TEXT("SCS"), EBlueprintTagTargetKind::Component, {});
    Add(TEXT("/Game/ActorOnly.ActorOnly"), TEXT("Actor"), TEXT("Actor Tags"), EBlueprintTagTargetKind::Actor, { TEXT("A") });
    Add(TEXT("/Game/ActorOnly.ActorOnly"), TEXT("EmptyBox"), TEXT("SCS"), EBlueprintTagTargetKind::Component, {});
    Add(TEXT("/Game/ComponentOnly.ComponentOnly"), TEXT("Actor"), TEXT("Actor Tags"), EBlueprintTagTargetKind::Actor, {});
    Add(TEXT("/Game/ComponentOnly.ComponentOnly"), TEXT("TaggedBox"), TEXT("SCS"), EBlueprintTagTargetKind::Component, { TEXT("C") });
    Add(TEXT("/Game/Full.Full"), TEXT("Actor"), TEXT("Actor Tags"), EBlueprintTagTargetKind::Actor, { TEXT("A") });
    Add(TEXT("/Game/Full.Full"), TEXT("TaggedBox"), TEXT("SCS"), EBlueprintTagTargetKind::Component, { TEXT("C") });
    Add(TEXT("/Game/Mixed.Mixed"), TEXT("Actor"), TEXT("Actor Tags"), EBlueprintTagTargetKind::Actor, {});
    Add(TEXT("/Game/Mixed.Mixed"), TEXT("TaggedBox"), TEXT("SCS"), EBlueprintTagTargetKind::Component, { TEXT("C") });
    Add(TEXT("/Game/Mixed.Mixed"), TEXT("EmptyBox"), TEXT("SCS"), EBlueprintTagTargetKind::Component, {});
    Add(TEXT("/Game/BareEmpty.BareEmpty"), TEXT("Actor"), TEXT("Actor Tags"), EBlueprintTagTargetKind::Actor, {});
    Add(TEXT("/Game/BareTagged.BareTagged"), TEXT("Actor"), TEXT("Actor Tags"), EBlueprintTagTargetKind::Actor, { TEXT("A") });
    auto FindGroup = [&Roots](const TCHAR* Path) -> TSharedPtr<FBlueprintTagOwnerRow>
    {
        const auto* Found = Roots.FindByPredicate([Path](const auto& Group) { return Group->AssetPath == Path; });
        return Found ? *Found : nullptr;
    };
    Filter = {}; Filter.ComponentPresence = EBlueprintTagPresence::Tagged; Build();
    TestEqual(TEXT("With tags keeps only Blueprints with actual actor or component tags"), Roots.Num(), 5);
    TestNull(TEXT("Fully empty Blueprint excluded from With tags"), FindGroup(TEXT("/Game/Empty.Empty")).Get());
    TestNull(TEXT("Empty actor without components excluded from With tags"), FindGroup(TEXT("/Game/BareEmpty.BareEmpty")).Get());
    TestTrue(TEXT("Actor-only tags qualify Blueprint"), FindGroup(TEXT("/Game/ActorOnly.ActorOnly")).IsValid());
    TestTrue(TEXT("Component-only tags qualify Blueprint even with untagged actor"), FindGroup(TEXT("/Game/ComponentOnly.ComponentOnly")).IsValid());
    TestEqual(TEXT("With tags includes tagged targets only, not untagged actor rows"), Matches.Num(), 6);
    TestTrue(TEXT("Every With tags target has a tag"), !Matches.ContainsByPredicate([](const auto& Row) { return (Row->Target.Tags.Num() == 0); }));
    Filter.ComponentPresence = EBlueprintTagPresence::Untagged; Build();
    TestEqual(TEXT("Without tags preserves all Blueprints, even fully empty and fully tagged ones"), Roots.Num(), 7);
    TestEqual(TEXT("Without tags keeps every actor and only untagged components"), Matches.Num(), 10);
    TestTrue(TEXT("Actor rows remain regardless of tags"), Matches.Contains(Rows[0]) && Matches.Contains(Rows[2]) && Matches.Contains(Rows[6]));
    TestTrue(TEXT("No tagged component passes Without tags"), !Matches.ContainsByPredicate([](const auto& Row)
        { return Row->Target.Kind != EBlueprintTagTargetKind::Actor && (Row->Target.Tags.Num() > 0); }));
    const auto Full = FindGroup(TEXT("/Game/Full.Full"));
    if (TestTrue(TEXT("Fully tagged Blueprint remains visible"), Full.IsValid()))
    {
        TestTrue(TEXT("Fully tagged actor and components get all-tagged indicator"), Full->AllTargetsHaveTags());
        TestEqual(TEXT("Fully tagged Blueprint has no displayed components"), Full->MatchingComponents, 0);
        TestEqual(TEXT("Fully tagged Blueprint still exposes actor"), Full->Children.Num(), 1);
        Full->SetChecked(true);
        TestTrue(TEXT("Group selection includes retained actor"), Rows[6]->bChecked);
        TestFalse(TEXT("Group selection excludes tagged component under Without tags"), Rows[7]->bChecked);
    }
    const auto ComponentOnly = FindGroup(TEXT("/Game/ComponentOnly.ComponentOnly"));
    if (TestTrue(TEXT("Component-only group found"), ComponentOnly.IsValid()))
    {
        TestFalse(TEXT("Untagged actor prevents all-tagged indicator"), ComponentOnly->AllTargetsHaveTags());
        TestFalse(TEXT("Actor tag presence is real data, not search-match state"), ComponentOnly->bActorHasTags);
    }
    const auto Empty = FindGroup(TEXT("/Game/Empty.Empty"));
    if (TestTrue(TEXT("Fully empty group found in Without tags"), Empty.IsValid()))
    {
        TestFalse(TEXT("Empty Blueprint has no actual tags"), Empty->HasAnyTags());
        TestFalse(TEXT("Empty Blueprint is not all-tagged"), Empty->AllTargetsHaveTags());
    }
    Filter = {}; Filter.ComponentPresence = EBlueprintTagPresence::Tagged; Filter.Blueprint = TEXT("Empty"); Build();
    TestEqual(TEXT("Text match cannot reintroduce tagless Blueprint in With tags"), Roots.Num(), 0);
    Filter = {}; Build();
    TestEqual(TEXT("All restores every Blueprint"), Roots.Num(), 7);
    TestEqual(TEXT("All restores every target"), Matches.Num(), 13);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBlueprintTagStaticMeshTest, "BlueprintTagManager.StaticMeshFilter",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FBlueprintTagStaticMeshTest::RunTest(const FString& Parameters)
{
    using namespace BlueprintTagManagerTests;
    UBlueprint* Blueprint = MakeBlueprint(AActor::StaticClass());
    if (!TestNotNull(TEXT("Mesh filter Blueprint"), Blueprint)) { return false; }
    USCS_Node* Box = Blueprint->SimpleConstructionScript->CreateNode(UBoxComponent::StaticClass(), TEXT("StaticMeshPretender"));
    Box->ComponentTemplate->ComponentTags = { TEXT("HiddenBoxTag") };
    Blueprint->SimpleConstructionScript->AddNode(Box);
    auto Add = [Blueprint, Box](UClass* Class, const TCHAR* Name, TArray<FName> Tags)
    {
        USCS_Node* Node = Blueprint->SimpleConstructionScript->CreateNode(Class, Name);
        Node->ComponentTemplate->ComponentTags = MoveTemp(Tags);
        Box->AddChildNode(Node);
    };
    Add(UStaticMeshComponent::StaticClass(), TEXT("MeshWithoutAsset"), {});
    Add(UInstancedStaticMeshComponent::StaticClass(), TEXT("Instanced"), { TEXT("ISMTag") });
    Add(UHierarchicalInstancedStaticMeshComponent::StaticClass(), TEXT("Hierarchical"), { TEXT("HISMTag") });
    Add(USkeletalMeshComponent::StaticClass(), TEXT("Skeletal"), { TEXT("SkeletalTag") });
    CastChecked<AActor>(Blueprint->GeneratedClass->GetDefaultObject())->Tags = { TEXT("ActorTag") };
    FKismetEditorUtilities::CompileBlueprint(Blueprint);
    TArray<TSharedPtr<FBlueprintTagOwnerRow>> Rows, Roots, Matches;
    for (auto Target : FBlueprintTagService::Gather(Blueprint))
    {
        auto Row = MakeShared<FBlueprintTagOwnerRow>();
        Row->Blueprint = Blueprint; Row->AssetPath = Blueprint->GetPathName(); Row->Target = MoveTemp(Target); Rows.Add(Row);
    }
    FBlueprintTagFilter Filter;
    auto Build = [&]() { FBlueprintTagView::Build(Rows, Filter, Roots, Matches); };
    Build();
    TestTrue(TEXT("StaticMesh only is on by default"), Filter.bStaticMeshOnly);
    TestFalse(TEXT("HISM exclusion is off by default"), Filter.bExcludeHISM);
    TestFalse(TEXT("ISM exclusion is off by default"), Filter.bExcludeISM);
    TestTrue(TEXT("ISM classified independently of HISM"), Rows.ContainsByPredicate([](const auto& Row)
        { return Row->Target.Label == TEXT("Instanced") && Row->Target.bInstancedStaticMeshComponent
            && !Row->Target.bHierarchicalInstancedStaticMeshComponent; }));
    TestTrue(TEXT("HISM does not inherit the ISM exclusion"), Rows.ContainsByPredicate([](const auto& Row)
        { return Row->Target.Label == TEXT("Hierarchical") && Row->Target.bHierarchicalInstancedStaticMeshComponent
            && !Row->Target.bInstancedStaticMeshComponent; }));
    TestEqual(TEXT("Actor plus StaticMesh, ISM, HISM match by real class"), Matches.Num(), 4);
    TestTrue(TEXT("Every displayed component derives from StaticMeshComponent"), !Matches.ContainsByPredicate([](const auto& Row)
        { return Row->Target.Kind != EBlueprintTagTargetKind::Actor && !Row->Target.bStaticMeshComponent; }));
    if (TestEqual(TEXT("One group"), Roots.Num(), 1))
    {
        TestEqual(TEXT("Whole-Blueprint component count is preserved"), Roots[0]->TotalComponents, 5);
        TestEqual(TEXT("Three StaticMesh component types shown"), Roots[0]->MatchingComponents, 3);
        TestFalse(TEXT("Summary excludes tags of hidden non-mesh components"), Roots[0]->Summary.Contains(TEXT("HiddenBoxTag")));
        TestTrue(TEXT("Summary includes matching component tags"),
            Roots[0]->Summary.EndsWith(TEXT("Components (shown): [HISMTag, ISMTag]")));
        Roots[0]->SetChecked(true);
        TestFalse(TEXT("Hidden components excluded from group batch selection"), Rows.ContainsByPredicate([](const auto& Row)
            { return Row->Target.Kind != EBlueprintTagTargetKind::Actor && !Row->Target.bStaticMeshComponent && Row->bChecked; }));
    }
    Filter.ComponentPresence = EBlueprintTagPresence::Tagged; Build();
    TestEqual(TEXT("Tagged filter combines with type filter"), Matches.Num(), 3);
    Filter.ComponentPresence = EBlueprintTagPresence::Untagged; Build();
    TestEqual(TEXT("Untagged filter keeps actor and empty static mesh"), Matches.Num(), 2);
    TestTrue(TEXT("Unassigned StaticMesh asset still qualifies by component class"), Matches.ContainsByPredicate([](const auto& Row)
        { return Row->Target.Label == TEXT("MeshWithoutAsset"); }));
    Filter = {}; Filter.Component = TEXT("StaticMeshPretender"); Build();
    TestEqual(TEXT("Class filter does not use component name"), Matches.Num(), 0);
    Filter = {}; Filter.bStaticMeshOnly = false; Build();
    TestEqual(TEXT("Turning off type filter restores all components and actor"), Matches.Num(), 6);
    for (const bool bMeshOnly : { false, true })
    {
        for (const bool bExcludeHISM : { false, true })
        {
            for (const bool bExcludeISM : { false, true })
            {
                for (const auto Presence : { EBlueprintTagPresence::All, EBlueprintTagPresence::Tagged, EBlueprintTagPresence::Untagged })
                {
                    Filter = {};
                    Filter.bStaticMeshOnly = bMeshOnly;
                    Filter.bExcludeHISM = bExcludeHISM;
                    Filter.bExcludeISM = bExcludeISM;
                    Filter.ComponentPresence = Presence;
                    for (const auto& Row : Rows) { Row->bChecked = false; }
                    Build();
                    TArray<FString> Expected;
                    // Fixed fixture expectations cover all 24 independent filter combinations.
                    if (Presence != EBlueprintTagPresence::Tagged) { Expected.Add(TEXT("MeshWithoutAsset")); }
                    if (Presence != EBlueprintTagPresence::Untagged)
                    {
                        if (!bExcludeHISM) { Expected.Add(TEXT("Hierarchical")); }
                        if (!bExcludeISM) { Expected.Add(TEXT("Instanced")); }
                        if (!bMeshOnly) { Expected.Add(TEXT("StaticMeshPretender")); Expected.Add(TEXT("Skeletal")); }
                    }
                    const FString Context = FString::Printf(TEXT("MeshOnly=%d, ExcludeHISM=%d, ExcludeISM=%d, Presence=%d"),
                        bMeshOnly, bExcludeHISM, bExcludeISM, static_cast<int32>(Presence));
                    TestEqual(Context + TEXT(": actor plus expected components"), Matches.Num(), Expected.Num() + 1);
                    for (const auto& Row : Matches)
                    {
                        TestTrue(Context + TEXT(": correct target ") + Row->Target.Label,
                            Row->Target.Kind == EBlueprintTagTargetKind::Actor || Expected.Contains(Row->Target.Label));
                    }
                    if (TestEqual(Context + TEXT(": Blueprint retained"), Roots.Num(), 1))
                    {
                        TestEqual(Context + TEXT(": overview includes every component"), Roots[0]->TotalComponents, 5);
                        TArray<FName> ExpectedTags;
                        if (Expected.Contains(TEXT("Hierarchical"))) { ExpectedTags.Add(TEXT("HISMTag")); }
                        if (Expected.Contains(TEXT("Instanced"))) { ExpectedTags.Add(TEXT("ISMTag")); }
                        if (Expected.Contains(TEXT("StaticMeshPretender"))) { ExpectedTags.Add(TEXT("HiddenBoxTag")); }
                        if (Expected.Contains(TEXT("Skeletal"))) { ExpectedTags.Add(TEXT("SkeletalTag")); }
                        ExpectedTags.Sort(FNameLexicalLess());
                        TestEqual(Context + TEXT(": summary contains only shown component tags"), Roots[0]->Summary,
                            FString(TEXT("Actor: [ActorTag]\nComponents (shown): ")) + FBlueprintTagService::FormatTags(ExpectedTags));
                        Roots[0]->SetChecked(true);
                        TSet<FString> Selected;
                        for (const auto& Row : Rows) { if (Row->bChecked) { Selected.Add(Row->Target.Id); } }
                        TestEqual(Context + TEXT(": excluded targets cannot be selected by group"), Selected.Num(), Expected.Num() + 1);
                        TestEqual(Context + TEXT(": batch preview respects exclusions"),
                            FBlueprintTagService::Preview(Blueprint, Selected, EBlueprintTagOperation::Add, NAME_None, TEXT("FilterBatch")).Num(),
                            Expected.Num() + 1);
                    }
                }
            }
        }
    }
    Filter = {}; Filter.bExcludeHISM = true; Filter.Component = TEXT("Hierarchical"); Build();
    TestEqual(TEXT("Component search does not bypass HISM exclusion"), Matches.Num(), 0);
    Filter = {}; Filter.bStaticMeshOnly = false; Filter.bExcludeISM = true; Filter.Tags = TEXT("ISMTag"); Build();
    TestEqual(TEXT("Tag search keeps HISM independently of ISM exclusion"), Matches.Num(), 1);
    TestTrue(TEXT("HISM survives ISM exclusion even though its tag also matches ISMTag"), Matches.Num() == 1
        && Matches[0]->Target.Label == TEXT("Hierarchical"));
    TestTrue(TEXT("Tag search summary contains only the surviving HISM tag"), Roots.Num() == 1
        && Roots[0]->Summary.EndsWith(TEXT("Components (shown): [HISMTag]")));
    UBlueprint* Child = MakeBlueprint(Blueprint->GeneratedClass);
    int32 InheritedMeshes = 0;
    int32 InheritedISM = 0, InheritedHISM = 0;
    Rows.Reset();
    for (const auto& Target : FBlueprintTagService::Gather(Child))
    {
        if (Target.Kind == EBlueprintTagTargetKind::InheritedComponent && Target.bStaticMeshComponent) { ++InheritedMeshes; }
        if (Target.Kind == EBlueprintTagTargetKind::InheritedComponent && Target.bInstancedStaticMeshComponent) { ++InheritedISM; }
        if (Target.Kind == EBlueprintTagTargetKind::InheritedComponent && Target.bHierarchicalInstancedStaticMeshComponent) { ++InheritedHISM; }
        auto Row = MakeShared<FBlueprintTagOwnerRow>();
        Row->Blueprint = Child; Row->AssetPath = Child->GetPathName(); Row->Target = Target; Rows.Add(Row);
    }
    TestEqual(TEXT("Inherited mesh component classes detected"), InheritedMeshes, 3);
    TestEqual(TEXT("Inherited ISM classified separately"), InheritedISM, 1);
    TestEqual(TEXT("Inherited HISM classified separately"), InheritedHISM, 1);
    Filter = {}; Filter.bExcludeHISM = true; Filter.bExcludeISM = true; Build();
    TestEqual(TEXT("Both exclusions apply to inherited components with StaticMesh only on"), Matches.Num(), 2);
    TestTrue(TEXT("Inherited excluded tags are absent from the summary"), Roots.Num() == 1
        && Roots[0]->Summary.EndsWith(TEXT("Components (shown): []")));
    Filter.bStaticMeshOnly = false; Build();
    TestEqual(TEXT("Both exclusions apply to inherited components with StaticMesh only off"), Matches.Num(), 4);
    UBlueprint* Native = MakeBlueprint(AStaticMeshActor::StaticClass());
    TestTrue(TEXT("Native C++ StaticMesh component detected"), FBlueprintTagService::Gather(Native).ContainsByPredicate([](const auto& Target)
        { return Target.Kind == EBlueprintTagTargetKind::NativeComponent && Target.bStaticMeshComponent; }));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBlueprintTagThumbnailTest, "BlueprintTagManager.ThumbnailDisplay",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FBlueprintTagThumbnailTest::RunTest(const FString& Parameters)
{
    using namespace BlueprintTagManagerTests;
    UStaticMesh* Cube = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube"));
    UStaticMesh* Sphere = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Sphere.Sphere"));
    UStaticMesh* Cone = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cone.Cone"));
    if (!TestNotNull(TEXT("Cube mesh"), Cube) || !TestNotNull(TEXT("Sphere mesh"), Sphere)
        || !TestNotNull(TEXT("Cone mesh"), Cone)) { return false; }
    const FString Name = TEXT("/Game/BTM_Thumbnails_") + FGuid::NewGuid().ToString(EGuidFormats::Digits) + TEXT("/BP_ThumbnailPreview");
    UPackage* Package = CreatePackage(*Name);
    UBlueprint* Blueprint = FKismetEditorUtilities::CreateBlueprint(AActor::StaticClass(), Package,
        FName(TEXT("BP_ThumbnailPreview")), BPTYPE_Normal, UBlueprint::StaticClass(), UBlueprintGeneratedClass::StaticClass());
    if (!TestNotNull(TEXT("Thumbnail Blueprint"), Blueprint)) { return false; }
    USCS_Node* Root = Blueprint->SimpleConstructionScript->CreateNode(UBoxComponent::StaticClass(), TEXT("NonMeshSensor"));
    Root->ComponentTemplate->ComponentTags = { TEXT("SensorTag") };
    Blueprint->SimpleConstructionScript->AddNode(Root);
    auto AddMesh = [Blueprint, Root](UClass* Class, const TCHAR* Label, UStaticMesh* Asset, FVector Location)
    {
        USCS_Node* Node = Blueprint->SimpleConstructionScript->CreateNode(Class, Label);
        auto* Component = CastChecked<UStaticMeshComponent>(Node->ComponentTemplate);
        Component->SetStaticMesh(Asset);
        Component->SetRelativeLocation(Location);
        if (Asset)
        {
            if (auto* Instanced = Cast<UInstancedStaticMeshComponent>(Component)) { Instanced->AddInstance(FTransform::Identity); }
        }
        Root->AddChildNode(Node);
        return Node;
    };
    USCS_Node* MeshNode = AddMesh(UStaticMeshComponent::StaticClass(), TEXT("StaticMesh_Cube"), Cube, FVector(-120, 0, 0));
    AddMesh(UInstancedStaticMeshComponent::StaticClass(), TEXT("ISM_Sphere"), Sphere, FVector::ZeroVector);
    AddMesh(UHierarchicalInstancedStaticMeshComponent::StaticClass(), TEXT("HISM_Cone"), Cone, FVector(120, 0, 0));
    AddMesh(UStaticMeshComponent::StaticClass(), TEXT("UnassignedMesh"), nullptr, FVector::ZeroVector);
    AddMesh(UInstancedStaticMeshComponent::StaticClass(), TEXT("UnassignedISM"), nullptr, FVector::ZeroVector);
    AddMesh(UHierarchicalInstancedStaticMeshComponent::StaticClass(), TEXT("UnassignedHISM"), nullptr, FVector::ZeroVector);
    CastChecked<AActor>(Blueprint->GeneratedClass->GetDefaultObject())->Tags = { TEXT("ActorTag") };
    FKismetEditorUtilities::CompileBlueprint(Blueprint);
    FAssetRegistryModule::AssetCreated(Blueprint);

    // Effective inherited overrides must show the child's mesh, not the parent's.
    UBlueprint* Child = MakeBlueprint(Blueprint->GeneratedClass);
    auto* Override = Cast<UStaticMeshComponent>(Child->GetInheritableComponentHandler(true)->CreateOverridenComponentTemplate(FComponentKey(MeshNode)));
    if (TestNotNull(TEXT("Child mesh override"), Override)) { Override->SetStaticMesh(Sphere); }
    int32 InheritedMeshes = 0;
    for (const auto& Target : FBlueprintTagService::Gather(Child))
    {
        if (Target.Label == TEXT("StaticMesh_Cube"))
        {
            FBlueprintTagOwnerRow Row; Row.Blueprint = Child; Row.Target = Target;
            TestTrue(TEXT("Inherited thumbnail uses the child's assigned asset"), SBlueprintTagManager::ResolveThumbnailAsset(Row) == Sphere);
            ++InheritedMeshes;
        }
    }
    TestEqual(TEXT("Inherited mesh target found once"), InheritedMeshes, 1);
    TestTrue(TEXT("Reading the child thumbnail preserves the parent asset"), CastChecked<UStaticMeshComponent>(MeshNode->ComponentTemplate)->GetStaticMesh() == Cube);
    UBlueprint* Native = MakeBlueprint(AStaticMeshActor::StaticClass());
    auto* NativeActor = CastChecked<AStaticMeshActor>(Native->GeneratedClass->GetDefaultObject());
    NativeActor->GetStaticMeshComponent()->SetStaticMesh(Cone);
    int32 NativeMeshes = 0;
    for (const auto& Target : FBlueprintTagService::Gather(Native))
    {
        if (Target.bStaticMeshComponent)
        {
            FBlueprintTagOwnerRow Row; Row.Blueprint = Native; Row.Target = Target;
            TestTrue(TEXT("Native component thumbnail uses its assigned asset"), SBlueprintTagManager::ResolveThumbnailAsset(Row) == Cone);
            ++NativeMeshes;
        }
    }
    TestEqual(TEXT("Native mesh target found once"), NativeMeshes, 1);
    auto& Kismet = FModuleManager::LoadModuleChecked<FBlueprintEditorModule>("Kismet");
    const int32 EditorsBefore = Kismet.GetBlueprintEditors().Num();
    const auto Tab = FGlobalTabmanager::Get()->TryInvokeTab(FName(TEXT("BlueprintTagManager.Project")));
    if (!TestTrue(TEXT("Thumbnail panel opens"), Tab.IsValid())) { FAssetRegistryModule::AssetDeleted(Blueprint); Package->SetDirtyFlag(false); return false; }
    const auto Widget = StaticCastSharedRef<SBlueprintTagManager>(Tab->GetContent());
    TestFalse(TEXT("Blueprint thumbnails default off"), Widget->GetThumbnailOptions().bBlueprints);
    TestFalse(TEXT("Mesh thumbnails default off"), Widget->GetThumbnailOptions().bStaticMeshComponents);
    TestEqual(TEXT("Default thumbnail size is 32 pixels"), Widget->GetThumbnailOptions().Size, 32);
    FBlueprintTagFilter Filter; Filter.Blueprint = Name; Filter.bStaticMeshOnly = false;
    Widget->SetFilter(Filter);
    Widget->RequestScan();
    for (int32 Frame = 0; Frame < 200 && Widget->IsScanInProgress(); ++Frame) { Widget->Tick(FGeometry(), FPlatformTime::Seconds(), 0.016f); }
    if (!TestEqual(TEXT("One thumbnail Blueprint found"), Widget->GetGroups().Num(), 1))
    { Tab->RequestCloseTab(); FAssetRegistryModule::AssetDeleted(Blueprint); Package->SetDirtyFlag(false); return false; }
    const auto Group = Widget->GetGroups()[0];
    TestTrue(TEXT("Blueprint group resolves to the Blueprint asset"), SBlueprintTagManager::ResolveThumbnailAsset(*Group) == Blueprint);
    TestEqual(TEXT("Actor and seven component targets"), Group->Children.Num(), 8);
    for (const auto& Row : Group->Children)
    {
        UObject* Asset = SBlueprintTagManager::ResolveThumbnailAsset(*Row);
        if (Row->Target.Label == TEXT("StaticMesh_Cube")) { TestTrue(TEXT("StaticMesh thumbnail is the cube asset"), Asset == Cube); }
        else if (Row->Target.Label == TEXT("ISM_Sphere")) { TestTrue(TEXT("ISM thumbnail is the sphere asset"), Asset == Sphere); }
        else if (Row->Target.Label == TEXT("HISM_Cone")) { TestTrue(TEXT("HISM thumbnail is the cone asset"), Asset == Cone); }
        else { TestNull(TEXT("Actor, non-mesh, and unassigned meshes have no thumbnail asset"), Asset); }
    }
    Group->SetChecked(true);
    Widget->ExpandGroups(true);
    const auto OriginalMatches = Widget->GetMatchingTargets();
    const FString OriginalSummary = Group->Summary;
    for (const bool bBlueprints : { false, true })
    {
        for (const bool bComponents : { false, true })
        {
            for (const int32 Size : { 16, 32, 64 })
            {
                FBlueprintTagThumbnailOptions Options; Options.bBlueprints = bBlueprints; Options.bStaticMeshComponents = bComponents; Options.Size = Size;
                Widget->SetThumbnailOptions(Options);
                TestEqual(TEXT("Chosen thumbnail size retained"), Widget->GetThumbnailOptions().Size, Size);
                TestEqual(TEXT("Blueprint thumbnail visibility is independent"), Widget->ShouldShowThumbnail(*Group), bBlueprints);
                for (const auto& Row : Group->Children)
                {
                    TestEqual(TEXT("Component visibility includes explicit No image placeholders"), Widget->ShouldShowThumbnail(*Row),
                        bComponents && Row->Target.Kind != EBlueprintTagTargetKind::Actor);
                    TestTrue(TEXT("Display settings preserve checked targets"), Row->bChecked);
                }
                TestTrue(TEXT("Display settings preserve batch matching scope"), Widget->GetMatchingTargets() == OriginalMatches);
                TestEqual(TEXT("Display settings preserve tag summaries"), Group->Summary, OriginalSummary);
                TestTrue(TEXT("Display settings preserve expansion"), Widget->IsGroupExpanded(Group));
            }
        }
    }
    FBlueprintTagThumbnailOptions Defaults;
    Widget->SetThumbnailOptions(Defaults);
    TestFalse(TEXT("Changing thumbnail settings does not start a scan"), Widget->IsScanInProgress());
    const bool bCapture = FParse::Param(FCommandLine::Get(), TEXT("BTMCaptureUI"));
    if (bCapture)
    {
        if (const auto Window = FSlateApplication::Get().FindWidgetWindow(Widget)) { Window->Resize(FVector2D(1440, 1800)); }
        auto QueueCapture = [this, Widget](bool bBlueprints, bool bComponents, int32 Size, const TCHAR* Filename)
        {
            ADD_LATENT_AUTOMATION_COMMAND(FFunctionLatentCommand([Widget, bBlueprints, bComponents, Size]()
            {
                FBlueprintTagThumbnailOptions Options; Options.bBlueprints = bBlueprints; Options.bStaticMeshComponents = bComponents; Options.Size = Size;
                Widget->SetThumbnailOptions(Options);
                return true;
            }));
            const FString OutputName(Filename);
            // Yield actual editor frames so the engine thumbnail pool can render.
            ADD_LATENT_AUTOMATION_COMMAND(FDelayedFunctionLatentCommand([this, Widget, OutputName]()
            {
                Widget->SlatePrepass();
                TArray<FColor> Pixels; FIntVector ImageSize;
                if (TestTrue(TEXT("Capture thumbnail options"), FSlateApplication::Get().TakeScreenshot(Widget, Pixels, ImageSize)))
                {
                    TArray64<uint8> PNG;
                    FImageUtils::PNGCompressImageArray(ImageSize.X, ImageSize.Y, Pixels, PNG);
                    TestTrue(TEXT("Save thumbnail capture"), FFileHelper::SaveArrayToFile(PNG, *FPaths::Combine(FPaths::ProjectSavedDir(), OutputName)));
                }
            }, 1.0f));
        };
        QueueCapture(false, false, 32, TEXT("BlueprintTagManager-Thumbnails-Off.png"));
        QueueCapture(true, false, 32, TEXT("BlueprintTagManager-Thumbnails-Blueprints.png"));
        QueueCapture(false, true, 32, TEXT("BlueprintTagManager-Thumbnails-Meshes.png"));
        QueueCapture(true, true, 16, TEXT("BlueprintTagManager-Thumbnails-Both16.png"));
        QueueCapture(true, true, 32, TEXT("BlueprintTagManager-Thumbnails-Both32.png"));
        QueueCapture(true, true, 64, TEXT("BlueprintTagManager-Thumbnails-Both64.png"));
    }
    ADD_LATENT_AUTOMATION_COMMAND(FFunctionLatentCommand([this, Tab, Widget, Blueprint, Package, EditorsBefore]()
    {
        auto& Module = FModuleManager::LoadModuleChecked<FBlueprintEditorModule>("Kismet");
        TestEqual(TEXT("Thumbnails do not open Blueprint editors"), Module.GetBlueprintEditors().Num(), EditorsBefore);
        TestTrue(TEXT("Thumbnails preserve actor tags"), CastChecked<AActor>(Blueprint->GeneratedClass->GetDefaultObject())->Tags == TArray<FName>({TEXT("ActorTag")}));
        Tab->RequestCloseTab();
        FAssetRegistryModule::AssetDeleted(Blueprint);
        Package->SetDirtyFlag(false);
        return true;
    }));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBlueprintTagRecentFoldersTest, "BlueprintTagManager.RecentFolders",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FBlueprintTagRecentFoldersTest::RunTest(const FString& Parameters)
{
    const FString Settings = FPaths::ConvertRelativePathToFull(FPaths::Combine(FPaths::ProjectSavedDir(),
        TEXT("Automation/BTM-History-") + FGuid::NewGuid().ToString(EGuidFormats::Digits) + TEXT(".ini")));
    FBlueprintTagRecentFolders History;
    History.Load(Settings);
    GConfig->SetString(TEXT("UnrelatedSection"), TEXT("Keep"), TEXT("Preserved"), Settings);
    TestEqual(TEXT("A new history is empty"), History.GetPaths().Num(), 0);
    for (const TCHAR* Path : { TEXT("/Game/A"), TEXT("/Game/B"), TEXT("/Game/C"), TEXT("/Game/D"), TEXT("/Game/E"), TEXT("/Game/F") })
    { TestTrue(TEXT("Remember a valid content folder"), History.Remember(Path)); }
    TestTrue(TEXT("Keep five unique paths, newest first"), History.GetPaths() == TArray<FString>({TEXT("/Game/F"), TEXT("/Game/E"), TEXT("/Game/D"), TEXT("/Game/C"), TEXT("/Game/B")}));
    TestTrue(TEXT("Reusing a folder moves it to the front"), History.Remember(TEXT(" /Game/D/ ")));
    TestTrue(TEXT("Whitespace and trailing slash are normalized"), History.GetPaths() == TArray<FString>({TEXT("/Game/D"), TEXT("/Game/F"), TEXT("/Game/E"), TEXT("/Game/C"), TEXT("/Game/B")}));
    TestFalse(TEXT("Case-insensitive duplicates do not create a sixth entry"), History.Remember(TEXT("/game/d")));
    const auto BeforeInvalid = History.GetPaths();
    for (const TCHAR* Invalid : { TEXT(""), TEXT(" "), TEXT("/"), TEXT("/Engine"), TEXT("/GameExtra/A"), TEXT("/Game/../Private"), TEXT("/Game/Bad Folder") })
    { TestFalse(TEXT("Invalid or out-of-scope paths are not stored"), History.Remember(Invalid)); }
    TestFalse(TEXT("Filesystem paths are not stored"), History.Remember(FPaths::ConvertRelativePathToFull(TEXT("Content"))));
    TestTrue(TEXT("Rejected paths leave history unchanged"), History.GetPaths() == BeforeInvalid);
    GConfig->UnloadFile(Settings);
    FBlueprintTagRecentFolders Reloaded;
    Reloaded.Load(Settings);
    TestTrue(TEXT("History survives reloading from disk"), Reloaded.GetPaths() == BeforeInvalid);
    FString Kept;
    TestTrue(TEXT("Other editor settings are preserved"), GConfig->GetString(TEXT("UnrelatedSection"), TEXT("Keep"), Kept, Settings) && Kept == TEXT("Preserved"));
    TestTrue(TEXT("Removing a recent path succeeds"), Reloaded.Remove(TEXT("/Game/D")));
    TestFalse(TEXT("Removing an absent path is a no-op"), Reloaded.Remove(TEXT("/Game/Missing")));
    GConfig->UnloadFile(Settings);
    FBlueprintTagRecentFolders AfterRemoval;
    AfterRemoval.Load(Settings);
    TestEqual(TEXT("Removal persists to disk"), AfterRemoval.GetPaths().Num(), 4);
    TestFalse(TEXT("Removed path stays absent"), AfterRemoval.GetPaths().Contains(TEXT("/Game/D")));

    const FString OtherSettings = Settings + TEXT(".other.ini");
    FBlueprintTagRecentFolders Other;
    Other.Load(OtherSettings);
    Other.Remember(TEXT("/Game/OtherProject"));
    AfterRemoval.Load(Settings);
    TestEqual(TEXT("Separate settings files do not share history"), AfterRemoval.GetPaths().Num(), 4);
    GConfig->SetArray(TEXT("BlueprintTagManager.RecentFolders"), TEXT("Paths"),
        {TEXT("/Game/A"), TEXT(" /Game/A/ "), TEXT("/Engine"), TEXT("/Game/B"), TEXT("/Game/C"), TEXT("/Game/D"), TEXT("/Game/E"), TEXT("/Game/F")}, OtherSettings);
    GConfig->Flush(false, OtherSettings);
    Other.Load(OtherSettings);
    TestTrue(TEXT("Loading sanitizes duplicates, invalid paths, and oversized history"), Other.GetPaths() == TArray<FString>({TEXT("/Game/A"), TEXT("/Game/B"), TEXT("/Game/C"), TEXT("/Game/D"), TEXT("/Game/E")}));

    const auto Widget = SNew(SBlueprintTagManager).RecentFoldersConfig(Settings);
    TestEqual(TEXT("Panel restores local folder history"), Widget->RecentFolderHistory.GetPaths().Num(), 4);
    Widget->UseRecentFolder(TEXT("/Game/F"));
    TestEqual(TEXT("Recent button fills Content folder"), Widget->FolderInput->GetText().ToString(), FString(TEXT("/Game/F")));
    TestFalse(TEXT("Using a recent folder does not request a scan"), Widget->IsScanInProgress());
    TestEqual(TEXT("Using a recent folder does not load Blueprints"), Widget->GetBlueprintCount(), 0);
    Widget->RemoveRecentFolder(TEXT("/Game/F"));
    TestEqual(TEXT("X removes a path from the panel"), Widget->RecentFolderHistory.GetPaths().Num(), 3);
    TestEqual(TEXT("Removing history does not clear the current input"), Widget->FolderInput->GetText().ToString(), FString(TEXT("/Game/F")));
    Widget->OnFolderCommitted(FText::FromString(TEXT("/Game/Partial")), ETextCommit::OnUserMovedFocus);
    TestEqual(TEXT("Focus changes do not save partial typing"), Widget->RecentFolderHistory.GetPaths().Num(), 3);
    Widget->FolderInput->SetText(FText::FromString(TEXT("/Game/Characters")));
    Widget->OnFolderCommitted(Widget->FolderInput->GetText(), ETextCommit::OnEnter);
    TestEqual(TEXT("Enter remembers a path without scanning"), Widget->RecentFolderHistory.GetPaths()[0], FString(TEXT("/Game/Characters")));
    TestFalse(TEXT("Enter remains independent of Scan project"), Widget->IsScanInProgress());
    const auto Reopened = SNew(SBlueprintTagManager).RecentFoldersConfig(Settings);
    TestTrue(TEXT("New panels restore the same recent paths"), Reopened->RecentFolderHistory.GetPaths() == Widget->RecentFolderHistory.GetPaths());

    // Populate a compact, readable fixture for visual review of the new layout.
    for (const FString& Path : TArray<FString>(Widget->RecentFolderHistory.GetPaths())) { Widget->RemoveRecentFolder(Path); }
    for (const TCHAR* Path : { TEXT("/Game/Props"), TEXT("/Game/Weapons"), TEXT("/Game/Environment"), TEXT("/Game/Characters"), TEXT("/Game") })
    { Widget->RememberFolder(Path); }
    Widget->UseRecentFolder(TEXT("/Game/Characters"));
    if (FParse::Param(FCommandLine::Get(), TEXT("BTMCaptureUI")))
    {
        const auto Window = SNew(SWindow).Title(FText::FromString(TEXT("Blueprint Tag Manager"))).ClientSize(FVector2D(2554, 1386))
            .SupportsMaximize(false).SupportsMinimize(false)[ Widget ];
        FSlateApplication::Get().AddWindow(Window);
        // The initial window creation can be clamped to the offscreen desktop.
        Window->Resize(FVector2D(2554, 1386));
        auto Capture = [this, Widget](const TCHAR* Filename)
        {
            Widget->SlatePrepass();
            TArray<FColor> Pixels; FIntVector Size;
            if (TestTrue(TEXT("Capture recent folder controls"), FSlateApplication::Get().TakeScreenshot(Widget, Pixels, Size)))
            {
                TArray64<uint8> PNG;
                FImageUtils::PNGCompressImageArray(Size.X, Size.Y, Pixels, PNG);
                TestTrue(TEXT("Save recent folder capture"), FFileHelper::SaveArrayToFile(PNG, *FPaths::Combine(FPaths::ProjectSavedDir(), Filename)));
            }
        };
        ADD_LATENT_AUTOMATION_COMMAND(FDelayedFunctionLatentCommand([Capture]() { Capture(TEXT("BlueprintTagManager-RecentFolders.png")); }, 0.25f));
        ADD_LATENT_AUTOMATION_COMMAND(FFunctionLatentCommand([Widget]() { Widget->RemoveRecentFolder(TEXT("/Game/Characters")); return true; }));
        ADD_LATENT_AUTOMATION_COMMAND(FDelayedFunctionLatentCommand([Capture]() { Capture(TEXT("BlueprintTagManager-RecentFolders-Removed.png")); }, 0.25f));
        ADD_LATENT_AUTOMATION_COMMAND(FFunctionLatentCommand([Window]() { Window->RequestDestroyWindow(); return true; }));
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBlueprintTagProjectBatchTest, "BlueprintTagManager.ProjectBatch",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FBlueprintTagProjectBatchTest::RunTest(const FString& Parameters)
{
    using namespace BlueprintTagManagerTests;
    UBlueprint* Parent = MakeBlueprint(AActor::StaticClass());
    USCS_Node* Box = Parent->SimpleConstructionScript->CreateNode(UBoxComponent::StaticClass(), TEXT("BatchBox"));
    Box->ComponentTemplate->ComponentTags = { TEXT("Shared"), TEXT("Keep") };
    Parent->SimpleConstructionScript->AddNode(Box);
    CastChecked<AActor>(Parent->GeneratedClass->GetDefaultObject())->Tags = { TEXT("Shared") };
    FKismetEditorUtilities::CompileBlueprint(Parent);
    UBlueprint* Child = MakeBlueprint(Parent->GeneratedClass);
    UBlueprint* Other = MakeBlueprint(AActor::StaticClass());
    CastChecked<AActor>(Other->GeneratedClass->GetDefaultObject())->Tags = { TEXT("Shared") };
    auto MakePlan = [](UBlueprint* Blueprint)
    {
        return FBlueprintTagService::Preview(Blueprint, AllEditable(Blueprint), EBlueprintTagOperation::Rename, TEXT("Shared"), TEXT("BatchNew"));
    };
    TArray<FBlueprintTagAssetChange> Batch = { { Parent, MakePlan(Parent) }, { Child, MakePlan(Child) }, { Other, MakePlan(Other) } };
    FText Error;
    CastChecked<AActor>(Other->GeneratedClass->GetDefaultObject())->Tags.Add(TEXT("External"));
    TestFalse(TEXT("Stale asset rejects entire project batch"), FBlueprintTagService::ApplyBatch(Batch, Error));
    TestTrue(TEXT("Earlier parent remains unchanged on rejection"), Box->ComponentTemplate->ComponentTags.Contains(TEXT("Shared")));
    TestNull(TEXT("Rejected batch creates no child overrides"), Child->GetInheritableComponentHandler(false));
    Batch[2].Changes = MakePlan(Other);
    TestTrue(TEXT("Parent, child, and unrelated asset apply together"), FBlueprintTagService::ApplyBatch(Batch, Error));
    TestTrue(TEXT("Parent component updated"), Box->ComponentTemplate->ComponentTags.Contains(TEXT("BatchNew")));
    TestTrue(TEXT("Child component updated through its own override"), Box->GetActualComponentTemplate(CastChecked<UBlueprintGeneratedClass>(Child->GeneratedClass))->ComponentTags.Contains(TEXT("BatchNew")));
    TestTrue(TEXT("Other asset updated"), CastChecked<AActor>(Other->GeneratedClass->GetDefaultObject())->Tags.Contains(TEXT("BatchNew")));
    GEditor->UndoTransaction();
    TestTrue(TEXT("One undo restores parent"), Box->ComponentTemplate->ComponentTags.Contains(TEXT("Shared")));
    TestTrue(TEXT("Same undo restores child"), Box->GetActualComponentTemplate(CastChecked<UBlueprintGeneratedClass>(Child->GeneratedClass))->ComponentTags.Contains(TEXT("Shared")));
    TestTrue(TEXT("Same undo restores unrelated asset"), CastChecked<AActor>(Other->GeneratedClass->GetDefaultObject())->Tags.Contains(TEXT("Shared")));
    GEditor->RedoTransaction();
    FKismetEditorUtilities::CompileBlueprint(Parent);
    FKismetEditorUtilities::CompileBlueprint(Child);
    FKismetEditorUtilities::CompileBlueprint(Other);
    TestTrue(TEXT("Parent tags persist after batch compile"), CastChecked<AActor>(Parent->GeneratedClass->GetDefaultObject())->Tags.Contains(TEXT("BatchNew")));
    TestTrue(TEXT("Child override persists after parent and child compile"), Box->GetActualComponentTemplate(CastChecked<UBlueprintGeneratedClass>(Child->GeneratedClass))->ComponentTags.Contains(TEXT("BatchNew")));
    TestTrue(TEXT("Unrelated asset persists after compile"), CastChecked<AActor>(Other->GeneratedClass->GetDefaultObject())->Tags.Contains(TEXT("BatchNew")));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBlueprintTagDiscoveryTest, "BlueprintTagManager.ProjectDiscovery",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FBlueprintTagDiscoveryTest::RunTest(const FString& Parameters)
{
    struct FFixture
    {
        FString Folder = TEXT("/Game/BTM_Discovery_") + FGuid::NewGuid().ToString(EGuidFormats::Digits);
        TArray<FString> ObjectPaths;
        TArray<FString> Files;
        ~FFixture()
        {
            TArray<UPackage*> Packages;
            for (const FString& Path : ObjectPaths)
            {
                if (UObject* Object = LoadObject<UObject>(nullptr, *Path))
                {
                    FAssetRegistryModule::AssetDeleted(Object);
                    Object->GetPackage()->SetDirtyFlag(false);
                    Packages.AddUnique(Object->GetPackage());
                }
            }
            UPackageTools::UnloadPackages(Packages);
            const FString ContentRoot = FPaths::ConvertRelativePathToFull(FPaths::ProjectContentDir());
            for (const FString& File : Files)
            {
                if (FPaths::IsUnderDirectory(FPaths::ConvertRelativePathToFull(File), ContentRoot))
                { IFileManager::Get().Delete(*File, false, true); }
            }
        }
        UBlueprint* Add(const FString& Name, UClass* Parent, bool bSibling = false)
        {
            const FString PackageName = Folder + (bSibling ? TEXT("Extra/") : TEXT("/")) + Name;
            UPackage* Package = CreatePackage(*PackageName);
            UBlueprint* Blueprint = FKismetEditorUtilities::CreateBlueprint(Parent, Package, FName(*Name), BPTYPE_Normal,
                UBlueprint::StaticClass(), UBlueprintGeneratedClass::StaticClass());
            if (Parent->IsChildOf(AActor::StaticClass()))
            {
                CastChecked<AActor>(Blueprint->GeneratedClass->GetDefaultObject())->Tags = { TEXT("DiskTag") };
            }
            FKismetEditorUtilities::CompileBlueprint(Blueprint);
            const FString File = FPackageName::LongPackageNameToFilename(PackageName, FPackageName::GetAssetPackageExtension());
            IFileManager::Get().MakeDirectory(*FPaths::GetPath(File), true);
            FSavePackageArgs Args;
            Args.TopLevelFlags = RF_Public | RF_Standalone;
            Args.SaveFlags = SAVE_NoError;
            ObjectPaths.Add(Blueprint->GetPathName()); Files.Add(File);
            FAssetRegistryModule::AssetCreated(Blueprint);
            if (!UPackage::SavePackage(Package, Blueprint, *File, Args)) { return nullptr; }
            return Blueprint;
        }
    } Fixture;
    UBlueprint* First = Fixture.Add(TEXT("First"), AActor::StaticClass());
    UBlueprint* Second = Fixture.Add(TEXT("Second"), AActor::StaticClass());
    UBlueprint* Component = Fixture.Add(TEXT("Component"), UActorComponent::StaticClass());
    UBlueprint* Sibling = Fixture.Add(TEXT("Sibling"), AActor::StaticClass(), true);
    if (!TestNotNull(TEXT("Saved first asset"), First) || !TestNotNull(TEXT("Saved second asset"), Second)
        || !TestNotNull(TEXT("Saved non-Actor Blueprint"), Component) || !TestNotNull(TEXT("Saved neighboring folder asset"), Sibling)) { return false; }
    TArray<UPackage*> Packages = { First->GetPackage(), Second->GetPackage(), Component->GetPackage(), Sibling->GetPackage() };
    FText Error;
    TestTrue(TEXT("Unload saved Blueprints before discovery"), UPackageTools::UnloadPackages(Packages, Error));
    IAssetRegistry& Registry = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry")).Get();
    Registry.ScanPathsSynchronous({ Fixture.Folder, Fixture.Folder + TEXT("Extra") }, true);
    Registry.WaitForCompletion();
    TArray<FAssetData> Assets;
    TestTrue(TEXT("Discover folder without Blueprint editors"), FBlueprintTagProject::Discover(Fixture.Folder, Assets, Error));
    TestEqual(TEXT("Folder boundary excludes similarly named sibling"), Assets.Num(), 3);
    int32 Actors = 0;
    for (const auto& Asset : Assets)
    {
        TestFalse(TEXT("Discovered asset is initially unloaded"), Asset.IsAssetLoaded());
        UBlueprint* Blueprint = Cast<UBlueprint>(Asset.GetAsset());
        if (FBlueprintTagService::CanEdit(Blueprint))
        {
            ++Actors;
            const auto Targets = FBlueprintTagService::Gather(Blueprint);
            TestTrue(TEXT("Actor tags can be read without opening the asset"), (Targets.Num() > 0) && Targets[0].Tags.Contains(TEXT("DiskTag")));
        }
    }
    TestEqual(TEXT("Non-Actor Blueprint excluded from tag editing"), Actors, 2);
    TestFalse(TEXT("Engine content is outside project editing scope"), FBlueprintTagProject::Discover(TEXT("/Engine"), Assets, Error));
    TestFalse(TEXT("Mount prefix boundary respected"), FBlueprintTagProject::IsInFolders(TEXT("/GameExtra/Test"), { TEXT("/Game") }));
    TestTrue(TEXT("Subfolder is in project scope"), FBlueprintTagProject::IsInFolders(TEXT("/Game/Foo/Test"), { TEXT("/Game") }));
    return true;
}

#endif
