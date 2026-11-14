#pragma once

#include "BlueprintTagCompilerCompatibility.h"
#include "CoreMinimal.h"

/** Project-local editor preferences; never written into distributable plugin config. */
class FBlueprintTagRecentFolders
{
public:
    void Load(const FString& IniFilename);
    bool Remember(const FString& Folder);
    bool Remove(const FString& Folder);
    const TArray<FString>& GetPaths() const { return Paths; }

private:
    static bool Normalize(const FString& Folder, FString& OutPath);
    void Save() const;
    TArray<FString> Paths;
    FString ConfigFilename;
};
