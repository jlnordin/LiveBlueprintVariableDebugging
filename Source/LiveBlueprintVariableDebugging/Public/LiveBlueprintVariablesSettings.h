#pragma once

#include "CoreMinimal.h"
#include "Engine/DeveloperSettings.h"

#include "LiveBlueprintVariablesSettings.generated.h"


UENUM()
enum class EShowBlueprintVariables : uint8
{
	OnlyWhenPlayingOrSimulating,
	Always
};


UENUM()
enum class EVariablesVisibilityFilter : uint8
{
	OnlyShowPublicVariables,
	ShowPublicAndProtectedVariables
};


UCLASS(Config = EditorPerProjectUserSettings)
class LIVEBLUEPRINTVARIABLEDEBUGGING_API ULiveBlueprintVariablesSettings : public UDeveloperSettings
{
	GENERATED_BODY()

public:
	// UDeveloperSettings overrides

	virtual FName GetContainerName() const { return FName("Project"); }
	virtual FName GetCategoryName() const { return FName("Plugins"); }
	virtual FName GetSectionName() const { return FName("LiveBlueprintVariablesSettings"); }

	virtual FText GetSectionText() const override
	{
		return NSLOCTEXT("LiveBlueprintVariables", "LiveBlueprintVariablesSettingsName", "Live Blueprint Variable Debugging");
	}

	virtual FText GetSectionDescription() const override
	{
		return NSLOCTEXT("LiveBlueprintVariables", "LiveBlueprintVariablesSettingsDescription", "Configure the Live Blueprint Variable Debugging plugin.");
	}

public:

	UPROPERTY(Config, EditAnywhere, Category = "Live Blueprint Variable Debugging")
		EShowBlueprintVariables WhenToShowVariables = EShowBlueprintVariables::OnlyWhenPlayingOrSimulating;

	UPROPERTY(Config, EditAnywhere, Category = "Live Blueprint Variable Debugging")
	bool bHighlightValuesThatHaveChanged = true;

	UPROPERTY(Config, EditAnywhere, Category = "Live Blueprint Variable Debugging")
	bool bShowHiddenImplementationVariables = false;
};
