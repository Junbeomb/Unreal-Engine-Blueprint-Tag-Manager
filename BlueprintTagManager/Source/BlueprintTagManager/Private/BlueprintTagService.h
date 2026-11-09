#pragma once

#include "CoreMinimal.h"
#include "UObject/WeakObjectPtr.h"

class UBlueprint;
class USCS_Node;

enum class EBlueprintTagOperation : uint8 { Add, Rename, Remove, Clear, Deduplicate };
enum class EBlueprintTagTargetKind : uint8 { Actor, Component, InheritedComponent, NativeComponent };

struct FBlueprintTagTarget
{
    FString Id;
    FString Label;
    FString Source;
    EBlueprintTagTargetKind Kind = EBlueprintTagTargetKind::Actor;
    TWeakObjectPtr<UObject> Object;
    TWeakObjectPtr<USCS_Node> Node;
    TArray<FName> Tags;
    bool bEditable = false;
};

struct FBlueprintTagChange
{
    FString Id;
    FString Label;
    TArray<FName> Before;
    TArray<FName> After;
};

struct FBlueprintTagAssetChange
{
    TWeakObjectPtr<UBlueprint> Blueprint;
    TArray<FBlueprintTagChange> Changes;
};

/** Serialized defaults only. Inherited components get overrides in each selected asset. */
class FBlueprintTagService
{
public:
    static bool CanEdit(const UBlueprint* Blueprint);
    static TArray<FBlueprintTagTarget> Gather(UBlueprint* Blueprint);
    static bool ParseTag(const FString& Input, FName& OutTag, FText& OutError);
    static TArray<FName> Transform(const TArray<FName>& Tags, EBlueprintTagOperation Operation,
        FName OldTag = NAME_None, FName NewTag = NAME_None);
    static TArray<FBlueprintTagChange> Preview(UBlueprint* Blueprint, const TSet<FString>& Selected,
        EBlueprintTagOperation Operation, FName OldTag, FName NewTag);
    static bool Apply(UBlueprint* Blueprint, const TArray<FBlueprintTagChange>& Changes, FText& OutError);
    static bool ApplyBatch(const TArray<FBlueprintTagAssetChange>& Assets, FText& OutError);
    static FString FormatTags(const TArray<FName>& Tags);
};
