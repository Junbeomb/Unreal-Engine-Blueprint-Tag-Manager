#include "BlueprintTagView.h"

namespace
{
    bool MatchesTerms(const FString& Value, const FString& Query)
    {
        TArray<FString> Terms;
        Query.ParseIntoArrayWS(Terms);
        for (const FString& Term : Terms)
        {
            if (!Value.Contains(Term, ESearchCase::IgnoreCase)) { return false; }
        }
        return true;
    }
}

bool FBlueprintTagFilter::Matches(const FString& AssetPath, const FBlueprintTagTarget& Target) const
{
    const bool bActor = Target.Kind == EBlueprintTagTargetKind::Actor;
    // A component-name query never matches the actor's Blueprint-derived label.
    if (bActor && !Component.TrimStartAndEnd().IsEmpty()) { return false; }
    if (!bActor && bStaticMeshOnly && !Target.bStaticMeshComponent) { return false; }
    if (!bActor && bExcludeHISM && Target.bHierarchicalInstancedStaticMeshComponent) { return false; }
    if (!bActor && bExcludeISM && Target.bInstancedStaticMeshComponent) { return false; }
    if (ComponentPresence == EBlueprintTagPresence::Tagged && (Target.Tags.Num() == 0)) { return false; }
    if (!bActor && ComponentPresence == EBlueprintTagPresence::Untagged && (Target.Tags.Num() > 0)) { return false; }
    return MatchesTerms(AssetPath, Blueprint) && MatchesTerms(bActor ? FString() : Target.Label, Component)
        && MatchesTerms(FString::JoinBy(Target.Tags, TEXT(" "),
            [](FName TagName) { return TagName.ToString(); }), Tags);
}

bool FBlueprintTagOwnerRow::CanCheck() const
{
    if (!bGroup) { return Target.bEditable; }
    return Children.ContainsByPredicate([](const auto& Child) { return Child->Target.bEditable; });
}

ECheckBoxState FBlueprintTagOwnerRow::CheckState() const
{
    if (!bGroup) { return bChecked ? ECheckBoxState::Checked : ECheckBoxState::Unchecked; }
    int32 Editable = 0, Checked = 0;
    for (const auto& Child : Children)
    {
        if (Child->Target.bEditable) { ++Editable; if (Child->bChecked) { ++Checked; } }
    }
    return Checked == 0 ? ECheckBoxState::Unchecked
        : Checked == Editable ? ECheckBoxState::Checked : ECheckBoxState::Undetermined;
}

void FBlueprintTagOwnerRow::SetChecked(bool bValue)
{
    if (bGroup) { for (const auto& Child : Children) { Child->SetChecked(bValue); } }
    else { bChecked = bValue && Target.bEditable; }
}

void FBlueprintTagView::Build(const TArray<TSharedPtr<FBlueprintTagOwnerRow>>& Rows, const FBlueprintTagFilter& Filter,
    TArray<TSharedPtr<FBlueprintTagOwnerRow>>& Roots, TArray<TSharedPtr<FBlueprintTagOwnerRow>>& Matches)
{
    Roots.Reset(); Matches.Reset();
    TMap<FString, TSharedPtr<FBlueprintTagOwnerRow>> Groups;
    TMap<FString, TSet<FName>> ShownComponentTags;
    TMap<FString, FString> ActorTags;
    for (const auto& Row : Rows)
    {
        auto& Group = Groups.FindOrAdd(Row->AssetPath);
        if (!Group)
        {
            Group = MakeShared<FBlueprintTagOwnerRow>();
            Group->bGroup = true;
            Group->Blueprint = Row->Blueprint;
            Group->AssetPath = Row->AssetPath;
            Roots.Add(Group);
        }
        const bool bActor = Row->Target.Kind == EBlueprintTagTargetKind::Actor;
        if (bActor)
        {
            ActorTags.Add(Row->AssetPath, FBlueprintTagService::FormatTags(Row->Target.Tags));
            Group->bActorHasTags = (Row->Target.Tags.Num() > 0);
        }
        else
        {
            ++Group->TotalComponents;
            if ((Row->Target.Tags.Num() > 0)) { ++Group->TaggedComponents; }
        }
        if (Filter.Matches(Row->AssetPath, Row->Target))
        {
            Matches.Add(Row); Group->Children.Add(Row);
            if (!bActor)
            {
                ++Group->MatchingComponents;
                for (FName TagName : Row->Target.Tags) { ShownComponentTags.FindOrAdd(Row->AssetPath).Add(TagName); }
            }
        }
    }
    Roots.RemoveAll([&Filter](const auto& Group)
    {
        return (Group->Children.Num() == 0)
            || (Filter.ComponentPresence == EBlueprintTagPresence::Tagged && !Group->HasAnyTags());
    });
    for (const auto& Group : Roots)
    {
        TArray<FName> Names;
        if (const auto* Unique = ShownComponentTags.Find(Group->AssetPath)) { Names = Unique->Array(); }
        Names.Sort(FNameLexicalLess());
        Group->Summary = TEXT("Actor: ") + ActorTags.FindRef(Group->AssetPath)
            + TEXT("\nComponents (shown): ") + FBlueprintTagService::FormatTags(Names);
    }
}
