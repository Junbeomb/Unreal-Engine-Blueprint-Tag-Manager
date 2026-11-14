#include "BlueprintTagRecentFolders.h"

#include "BlueprintTagProject.h"
#include "Misc/ConfigCacheIni.h"
#include "Misc/PackageName.h"

namespace
{
    const TCHAR* RecentFoldersSection = TEXT("BlueprintTagManager.RecentFolders");
    const TCHAR* RecentFoldersKey = TEXT("Paths");
    constexpr int32 MaxRecentFolders = 5;
}

bool FBlueprintTagRecentFolders::Normalize(const FString& Folder, FString& OutPath)
{
    OutPath = Folder.TrimStartAndEnd().Replace(TEXT("\\"), TEXT("/"));
    while (OutPath.EndsWith(TEXT("/"))) { OutPath.LeftChopInline(1); }
    // Validate as a folder by appending a dummy package name, including /Game itself.
    return !OutPath.IsEmpty() && !OutPath.Contains(TEXT("//"))
        && FBlueprintTagProject::IsInFolders(OutPath, FBlueprintTagProject::GetContentRoots())
        && FPackageName::IsValidLongPackageName(OutPath + TEXT("/__BTMFolder__"));
}

void FBlueprintTagRecentFolders::Load(const FString& IniFilename)
{
    ConfigFilename = IniFilename;
    Paths.Reset();
    TArray<FString> Saved;
    if (GConfig && !ConfigFilename.IsEmpty())
    {
        // UE 5.7 does not create missing files through SetArray, and Find can
        // register an existing standalone file as read-only. Register it
        // explicitly while leaving the editor's already-loaded settings intact.
        if (!GConfig->FindConfigFile(ConfigFilename))
        {
            FConfigFile File;
            File.Read(ConfigFilename);
            GConfig->Add(ConfigFilename, File);
        }
        GConfig->GetArray(RecentFoldersSection, RecentFoldersKey, Saved, ConfigFilename);
    }
    for (const FString& Folder : Saved)
    {
        FString Path;
        if (Normalize(Folder, Path) && !Paths.ContainsByPredicate([&Path](const FString& Item) { return Item.Equals(Path, ESearchCase::IgnoreCase); }))
        {
            Paths.Add(Path);
            if (Paths.Num() == MaxRecentFolders) { break; }
        }
    }
}

bool FBlueprintTagRecentFolders::Remember(const FString& Folder)
{
    FString Path;
    if (!Normalize(Folder, Path)) { return false; }
    if (Paths.Num() > 0 && Paths[0].Equals(Path, ESearchCase::IgnoreCase)) { return false; }
    Paths.RemoveAll([&Path](const FString& Item) { return Item.Equals(Path, ESearchCase::IgnoreCase); });
    Paths.Insert(Path, 0);
    if (Paths.Num() > MaxRecentFolders) { Paths.SetNum(MaxRecentFolders); }
    Save();
    return true;
}

bool FBlueprintTagRecentFolders::Remove(const FString& Folder)
{
    if (Paths.RemoveAll([&Folder](const FString& Item) { return Item.Equals(Folder, ESearchCase::IgnoreCase); }) == 0) { return false; }
    Save();
    return true;
}

void FBlueprintTagRecentFolders::Save() const
{
    if (GConfig && !ConfigFilename.IsEmpty())
    {
        GConfig->SetArray(RecentFoldersSection, RecentFoldersKey, Paths, ConfigFilename);
        GConfig->Flush(false, ConfigFilename);
    }
}
