#include "BlueprintTagService.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "BlueprintEditor.h"
#include "BlueprintEditorModule.h"
#include "BlueprintTagProject.h"
#include "SBlueprintTagManager.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "AssetRegistry/IAssetRegistry.h"
#include "Components/BoxComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Editor.h"
#include "Engine/Blueprint.h"
#include "Engine/BlueprintGeneratedClass.h"
#include "Engine/InheritableComponentHandler.h"
#include "Engine/SCS_Node.h"
#include "Engine/SimpleConstructionScript.h"
#include "Engine/StaticMeshActor.h"
#include "GameFramework/Actor.h"
#include "Framework/Docking/TabManager.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "Misc/AutomationTest.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"
#include "HAL/FileManager.h"
#include "PackageTools.h"
#include "UObject/Package.h"
#include "UObject/SavePackage.h"
#include "Widgets/Docking/SDockTab.h"

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
    TestEqual(TEXT("Deduplicate"), FBlueprintTagService::Transform(Original, Op::Deduplicate).Num(), 3);
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
    Add(TEXT("/Game/Hero.Hero"), TEXT("Sensor"), TEXT("SCS"), EBlueprintTagTargetKind::Component, { TEXT("Enemy"), TEXT("Enemy") });
    Add(TEXT("/Game/Hero.Hero"), TEXT("Enemy"), TEXT("Native"), EBlueprintTagTargetKind::NativeComponent, {});
    Add(TEXT("/Game/Enemy.Enemy"), TEXT("Other"), TEXT("EnemyOrigin"), EBlueprintTagTargetKind::Component, { TEXT("Hero") });
    FBlueprintTagFilter Filter;
    auto Build = [&]() { FBlueprintTagView::Build(Rows, Filter, Roots, Matches); };
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
    Roots[0]->SetChecked(false);
    TestFalse(TEXT("Filtered group selection changes matching target"), Rows[1]->bChecked);
    TestTrue(TEXT("Filtered group selection preserves excluded target"), Rows[0]->bChecked);
    Filter = {}; Filter.Blueprint = TEXT("Enemy"); Build();
    TestTrue(TEXT("Blueprint search ignores tags and component names"), Matches.Num() == 1 && Matches[0] == Rows[3]);
    Filter = {}; Filter.Component = TEXT("Enemy"); Build();
    TestTrue(TEXT("Component search ignores Blueprint, source and tag values"), Matches.Num() == 1 && Matches[0] == Rows[2]);
    Filter = {}; Filter.Source = TEXT("Enemy"); Build();
    TestTrue(TEXT("Source search uses only source"), Matches.Num() == 1 && Matches[0] == Rows[3]);
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
    TestTrue(TEXT("Every With tags target has a tag"), !Matches.ContainsByPredicate([](const auto& Row) { return Row->Target.Tags.IsEmpty(); }));
    Filter.ComponentPresence = EBlueprintTagPresence::Untagged; Build();
    TestEqual(TEXT("Without tags preserves all Blueprints, even fully empty and fully tagged ones"), Roots.Num(), 7);
    TestEqual(TEXT("Without tags keeps every actor and only untagged components"), Matches.Num(), 10);
    TestTrue(TEXT("Actor rows remain regardless of tags"), Matches.Contains(Rows[0]) && Matches.Contains(Rows[2]) && Matches.Contains(Rows[6]));
    TestTrue(TEXT("No tagged component passes Without tags"), !Matches.ContainsByPredicate([](const auto& Row)
        { return Row->Target.Kind != EBlueprintTagTargetKind::Actor && !Row->Target.Tags.IsEmpty(); }));
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
            TestTrue(TEXT("Actor tags can be read without opening the asset"), !Targets.IsEmpty() && Targets[0].Tags.Contains(TEXT("DiskTag")));
        }
    }
    TestEqual(TEXT("Non-Actor Blueprint excluded from tag editing"), Actors, 2);
    TestFalse(TEXT("Engine content is outside project editing scope"), FBlueprintTagProject::Discover(TEXT("/Engine"), Assets, Error));
    TestFalse(TEXT("Mount prefix boundary respected"), FBlueprintTagProject::IsInFolders(TEXT("/GameExtra/Test"), { TEXT("/Game") }));
    TestTrue(TEXT("Subfolder is in project scope"), FBlueprintTagProject::IsInFolders(TEXT("/Game/Foo/Test"), { TEXT("/Game") }));
    return true;
}

#endif
