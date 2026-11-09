#include "BlueprintTagService.h"

#include "Components/ActorComponent.h"
#include "Editor.h"
#include "Engine/Blueprint.h"
#include "Engine/BlueprintGeneratedClass.h"
#include "Engine/InheritableComponentHandler.h"
#include "Engine/SCS_Node.h"
#include "Engine/SimpleConstructionScript.h"
#include "GameFramework/Actor.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Misc/ConfigCacheIni.h"
#include "ScopedTransaction.h"
#include "UObject/UnrealType.h"

#define LOCTEXT_NAMESPACE "BlueprintTagManager"

bool FBlueprintTagService::CanEdit(const UBlueprint* Blueprint)
{
    return IsValid(Blueprint) && Blueprint->GeneratedClass
        && Blueprint->GeneratedClass->IsChildOf(AActor::StaticClass())
        && !Blueprint->bBeingCompiled && (!GEditor || !GEditor->PlayWorld);
}

TArray<FBlueprintTagTarget> FBlueprintTagService::Gather(UBlueprint* Blueprint)
{
    TArray<FBlueprintTagTarget> Result;
    if (!CanEdit(Blueprint)) { return Result; }
    UBlueprintGeneratedClass* Class = Cast<UBlueprintGeneratedClass>(Blueprint->GeneratedClass);
    AActor* CDO = Class ? Cast<AActor>(Class->GetDefaultObject()) : nullptr;
    if (!CDO) { return Result; }

    FBlueprintTagTarget& Actor = Result.AddDefaulted_GetRef();
    Actor.Id = TEXT("Actor");
    Actor.Label = Blueprint->GetName() + TEXT(" (Actor)");
    Actor.Source = TEXT("Actor Tags");
    Actor.Object = CDO;
    Actor.Tags = CDO->Tags;
    Actor.bEditable = true;

    bool bEnableInherited = true;
    GConfig->GetBool(TEXT("Kismet"), TEXT("bEnableInheritableComponents"), bEnableInherited, GEngineIni);
    for (UClass* Cursor = Class; Cursor; Cursor = Cursor->GetSuperClass())
    {
        UBlueprintGeneratedClass* OwnerClass = Cast<UBlueprintGeneratedClass>(Cursor);
        if (!OwnerClass || !OwnerClass->SimpleConstructionScript) { continue; }
        for (USCS_Node* Node : OwnerClass->SimpleConstructionScript->GetAllNodes())
        {
            if (!IsValid(Node)) { continue; }
            UActorComponent* Template = Node->GetActualComponentTemplate(Class);
            if (!IsValid(Template)) { continue; }
            const bool bInherited = OwnerClass != Class;
            FBlueprintTagTarget& Target = Result.AddDefaulted_GetRef();
            Target.Id = OwnerClass->GetPathName() + TEXT(":") + Node->GetVariableName().ToString();
            Target.Label = Node->GetVariableName().ToString();
            Target.Source = (bInherited ? TEXT("Inherited: ") : TEXT("Component: "))
                + GetNameSafe(OwnerClass->ClassGeneratedBy);
            Target.Kind = bInherited ? EBlueprintTagTargetKind::InheritedComponent : EBlueprintTagTargetKind::Component;
            Target.Object = Template;
            Target.Node = Node;
            Target.Tags = Template->ComponentTags;
            Target.bEditable = !bInherited || (bEnableInherited && Template->IsEditableWhenInherited()
                && FComponentKey(Node).IsValid());
        }
    }

    TInlineComponentArray<UActorComponent*> NativeComponents(CDO);
    for (UActorComponent* Component : NativeComponents)
    {
        if (!IsValid(Component) || Component->CreationMethod != EComponentCreationMethod::Native) { continue; }
        FBlueprintTagTarget& Target = Result.AddDefaulted_GetRef();
        Target.Id = TEXT("Native:") + Component->GetPathName(CDO);
        Target.Label = Component->GetName();
        Target.Source = TEXT("Native: ") + Component->GetClass()->GetName();
        Target.Kind = EBlueprintTagTargetKind::NativeComponent;
        Target.Object = Component;
        Target.Tags = Component->ComponentTags;
        Target.bEditable = Component->IsIn(CDO) && Component->IsEditableWhenInherited();
    }
    return Result;
}

bool FBlueprintTagService::ParseTag(const FString& Input, FName& OutTag, FText& OutError)
{
    const FString Value = Input.TrimStartAndEnd();
    if (Value.IsEmpty() || Value.Len() >= NAME_SIZE || Value.Contains(TEXT("\n")) || Value.Contains(TEXT("\r")))
    {
        OutError = LOCTEXT("InvalidTag", "Enter one non-empty tag (under 1024 characters, without line breaks).");
        return false;
    }
    OutTag = FName(*Value);
    if (OutTag.IsNone())
    {
        OutError = LOCTEXT("NoneTag", "None is the empty FName value. Choose another tag.");
        return false;
    }
    return true;
}

TArray<FName> FBlueprintTagService::Transform(const TArray<FName>& Tags, EBlueprintTagOperation Operation,
    FName OldTag, FName NewTag)
{
    TArray<FName> Result = Tags;
    switch (Operation)
    {
    case EBlueprintTagOperation::Add:
        if (!NewTag.IsNone()) { Result.AddUnique(NewTag); }
        break;
    case EBlueprintTagOperation::Rename:
        if (!OldTag.IsNone() && !NewTag.IsNone() && OldTag != NewTag && Tags.Contains(OldTag))
        {
            Result.Reset();
            bool bAddedReplacement = false;
            for (const FName Tag : Tags)
            {
                if (Tag == OldTag || Tag == NewTag)
                {
                    if (!bAddedReplacement) { Result.Add(NewTag); bAddedReplacement = true; }
                }
                else { Result.Add(Tag); }
            }
        }
        break;
    case EBlueprintTagOperation::Remove:
        Result.Remove(OldTag);
        break;
    case EBlueprintTagOperation::Clear:
        Result.Reset();
        break;
    case EBlueprintTagOperation::Deduplicate:
        Result.Reset();
        for (const FName Tag : Tags) { Result.AddUnique(Tag); }
        break;
    }
    return Result;
}

TArray<FBlueprintTagChange> FBlueprintTagService::Preview(UBlueprint* Blueprint, const TSet<FString>& Selected,
    EBlueprintTagOperation Operation, FName OldTag, FName NewTag)
{
    TArray<FBlueprintTagChange> Changes;
    for (const FBlueprintTagTarget& Target : Gather(Blueprint))
    {
        if (!Target.bEditable || !Selected.Contains(Target.Id)) { continue; }
        TArray<FName> After = Transform(Target.Tags, Operation, OldTag, NewTag);
        if (After != Target.Tags) { Changes.Add({Target.Id, Target.Label, Target.Tags, MoveTemp(After)}); }
    }
    return Changes;
}

bool FBlueprintTagService::Apply(UBlueprint* Blueprint, const TArray<FBlueprintTagChange>& Changes, FText& OutError)
{
    return ApplyBatch({ { Blueprint, Changes } }, OutError);
}

bool FBlueprintTagService::ApplyBatch(const TArray<FBlueprintTagAssetChange>& Assets, FText& OutError)
{
    struct FResolvedChange
    {
        UBlueprint* Blueprint = nullptr;
        FBlueprintTagTarget Target;
        FBlueprintTagChange Change;
        UObject* Destination = nullptr;
        int32 Depth = 0;
    };
    TArray<FResolvedChange> Resolved;
    TSet<UBlueprint*> SeenAssets;
    // Validate the entire preview before creating overrides or modifying any asset.
    for (const FBlueprintTagAssetChange& Asset : Assets)
    {
        UBlueprint* Blueprint = Asset.Blueprint.Get();
        if (!CanEdit(Blueprint) || Asset.Changes.IsEmpty() || SeenAssets.Contains(Blueprint))
        {
            OutError = LOCTEXT("Unavailable", "An asset is unavailable, the preview is empty, or Play/Simulate is running. Refresh and preview again.");
            return false;
        }
        SeenAssets.Add(Blueprint);
        const TArray<FBlueprintTagTarget> Current = Gather(Blueprint);
        TSet<FString> SeenTargets;
        int32 Depth = 0;
        for (UClass* Class = Blueprint->GeneratedClass; Class; Class = Class->GetSuperClass()) { ++Depth; }
        for (const FBlueprintTagChange& Change : Asset.Changes)
        {
            const FBlueprintTagTarget* Target = Current.FindByPredicate(
                [&Change](const FBlueprintTagTarget& Item) { return Item.Id == Change.Id; });
            if (!Target || !Target->bEditable || !Target->Object.IsValid()
                || Target->Tags != Change.Before || SeenTargets.Contains(Change.Id))
            {
                OutError = FText::Format(LOCTEXT("StaleBatch", "{0} changed after preview. Refresh and preview again. No tags were changed."),
                    FText::FromString(Blueprint->GetPathName()));
                return false;
            }
            SeenTargets.Add(Change.Id);
            Resolved.Add({ Blueprint, *Target, Change, nullptr, Depth });
        }
    }
    if (Resolved.IsEmpty())
    {
        OutError = LOCTEXT("EmptyBatch", "Preview a change before applying it.");
        return false;
    }

    // Empty handlers are persistent editing infrastructure, as in the normal
    // component details editor. Create them before the tag transaction. Recording
    // entire parent/child UClasses makes UClass::Serialize preload a class that is
    // also being restored, which UE's transactor cannot safely do in one Undo.
    TMap<UBlueprint*, UInheritableComponentHandler*> Handlers;
    for (const FResolvedChange& Item : Resolved)
    {
        if (Item.Target.Kind == EBlueprintTagTargetKind::InheritedComponent && !Handlers.Contains(Item.Blueprint))
        {
            UInheritableComponentHandler* Handler = Item.Blueprint->GetInheritableComponentHandler(true);
            if (!Handler)
            {
                OutError = LOCTEXT("NoHandler", "An inherited component handler could not be created. No tags were changed.");
                return false;
            }
            Handler->SetFlags(RF_Transactional);
            Handlers.Add(Item.Blueprint, Handler);
        }
    }
    const FScopedTransaction Transaction(LOCTEXT("Transaction", "Edit Blueprint Actor and Component Tags"));
    for (UBlueprint* Blueprint : SeenAssets) { Blueprint->Modify(); }
    TArray<TPair<UInheritableComponentHandler*, FComponentKey>> CreatedOverrides;
    // Materialize all child overrides against the old defaults before editing any parent.
    for (FResolvedChange& Item : Resolved)
    {
        Item.Destination = Item.Target.Object.Get();
        if (Item.Target.Kind == EBlueprintTagTargetKind::InheritedComponent)
        {
            UInheritableComponentHandler* Handler = Handlers.FindChecked(Item.Blueprint);
            if (Handler)
            {
                Handler->SetFlags(RF_Transactional);
                Handler->Modify();
                const FComponentKey Key(Item.Target.Node.Get());
                Item.Destination = Handler->GetOverridenComponentTemplate(Key);
                if (!Item.Destination)
                {
                    Item.Destination = Handler->CreateOverridenComponentTemplate(Key);
                    if (Item.Destination) { CreatedOverrides.Emplace(Handler, Key); }
                }
            }
            else { Item.Destination = nullptr; }
        }
        if (!Item.Destination)
        {
            for (const auto& Created : CreatedOverrides)
            {
                Created.Key->RemoveOverridenComponentTemplate(Created.Value);
            }
            OutError = LOCTEXT("ResolveFailed", "Could not create an inherited component override. No tags were changed in any asset.");
            return false;
        }
        Item.Destination->SetFlags(RF_Transactional);
        Item.Destination->Modify();
    }

    Resolved.StableSort([](const FResolvedChange& A, const FResolvedChange& B) { return A.Depth > B.Depth; });
    for (const FResolvedChange& Item : Resolved)
    {
        UObject* Object = Item.Destination;
        const bool bActor = Item.Target.Kind == EBlueprintTagTargetKind::Actor;
        FProperty* Property = bActor
            ? FindFProperty<FProperty>(AActor::StaticClass(), GET_MEMBER_NAME_CHECKED(AActor, Tags))
            : FindFProperty<FProperty>(UActorComponent::StaticClass(), GET_MEMBER_NAME_CHECKED(UActorComponent, ComponentTags));
        Object->PreEditChange(Property);
        if (bActor) { CastChecked<AActor>(Object)->Tags = Item.Change.After; }
        else { CastChecked<UActorComponent>(Object)->ComponentTags = Item.Change.After; }
        FPropertyChangedEvent Event(Property, EPropertyChangeType::ValueSet);
        Object->PostEditChangeProperty(Event);
    }
    for (UBlueprint* Blueprint : SeenAssets) { FBlueprintEditorUtils::MarkBlueprintAsModified(Blueprint); }
    return true;
}

FString FBlueprintTagService::FormatTags(const TArray<FName>& Tags)
{
    TArray<FString> Names;
    for (const FName Tag : Tags) { Names.Add(Tag.ToString()); }
    return TEXT("[") + FString::Join(Names, TEXT(", ")) + TEXT("]");
}

#undef LOCTEXT_NAMESPACE
