#pragma once

#include "CoreMinimal.h"
#include "Engine/DeveloperSettings.h"

#include "LiveBlueprintDebuggerSettings.generated.h"


UENUM()
enum class EShowBlueprintVariables : uint8
{
	OnlyWhenPlayingOrSimulating,
	Always
};


UCLASS(Config = EditorPerProjectUserSettings)
class LIVEBLUEPRINTDEBUGGER_API ULiveBlueprintDebuggerSettings : public UDeveloperSettings
{
	GENERATED_BODY()

public:
	// UDeveloperSettings overrides

	virtual FName GetContainerName() const { return FName("Project"); }
	virtual FName GetCategoryName() const { return FName("Plugins"); }
	virtual FName GetSectionName() const { return FName("LiveBlueprintDebugger"); }

	virtual FText GetSectionText() const override
	{
		return NSLOCTEXT("LiveBlueprintDebugger", "LiveBlueprintDebuggerSettingsName", "Live Blueprint Debugger");
	}

	virtual FText GetSectionDescription() const override
	{
		return NSLOCTEXT("LiveBlueprintDebugger", "LiveBlueprintDebuggerSettingsDescription", "Configure the Live Blueprint Debugger plugin.");
	}

public:

	UPROPERTY(Config, EditAnywhere, Category = "Live Blueprint Debugger")
	EShowBlueprintVariables WhenToShowVariables = EShowBlueprintVariables::OnlyWhenPlayingOrSimulating;

	UPROPERTY(Config, EditAnywhere, Category = "Live Blueprint Debugger")
	bool bHighlightValuesThatHaveChanged = true;
};
