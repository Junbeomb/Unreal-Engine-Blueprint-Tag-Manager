#pragma once

#include "BlueprintTagCompilerCompatibility.h"
#include "Runtime/Launch/Resources/Version.h"

// UE 5.0 uses the editor style set; later releases use the application style.
#if ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION == 0
#include "EditorStyleSet.h"
using FBlueprintTagStyle = FEditorStyle;
#else
#include "Styling/AppStyle.h"
using FBlueprintTagStyle = FAppStyle;
#endif
