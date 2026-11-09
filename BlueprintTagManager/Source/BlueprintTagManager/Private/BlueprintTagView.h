#pragma once

#include "BlueprintTagService.h"
#include "Styling/SlateTypes.h"

enum class EBlueprintTagPresence { All, Tagged, Untagged };

struct FBlueprintTagFilter
{
    FString Blueprint;
    FString Component;
    FString Tags;
    bool bStaticMeshOnly = true;
    EBlueprintTagPresence ComponentPresence = EBlueprintTagPresence::All;
    bool Matches(const FString& AssetPath, const FBlueprintTagTarget& Target) const;
};

struct FBlueprintTagOwnerRow
{
    TWeakObjectPtr<UBlueprint> Blueprint;
    FString AssetPath;
    FBlueprintTagTarget Target;
    bool bChecked = false;
    bool bGroup = false;
    int32 TotalComponents = 0;
    int32 MatchingComponents = 0;
    int32 TaggedComponents = 0;
    bool bActorHasTags = false;
    bool HasAnyTags() const { return bActorHasTags || TaggedComponents > 0; }
    bool AllTargetsHaveTags() const { return bActorHasTags && TaggedComponents == TotalComponents; }
    FString Summary;
    TArray<TSharedPtr<FBlueprintTagOwnerRow>> Children;

    FString Key() const { return AssetPath + TEXT("\n") + Target.Id; }
    bool CanCheck() const;
    ECheckBoxState CheckState() const;
    void SetChecked(bool bValue);
};

class FBlueprintTagView
{
public:
    // Matching targets are independent of the tree's expansion state.
    static void Build(const TArray<TSharedPtr<FBlueprintTagOwnerRow>>& Rows, const FBlueprintTagFilter& Filter,
        TArray<TSharedPtr<FBlueprintTagOwnerRow>>& Roots, TArray<TSharedPtr<FBlueprintTagOwnerRow>>& Matches);
};
