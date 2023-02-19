// Copyright (c) 2022-2023 Justin Nordin. All Rights Reserved.

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


UENUM()
enum class EPropertyRefreshRate : uint8
{
	NoLiveUpdates	UMETA(DisplayName = "No Live Updates"),
	One				UMETA(DisplayName = "1Hz"),
	Ten				UMETA(DisplayName = "10Hz"),
	Thirty			UMETA(DisplayName = "30Hz")
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

	UPROPERTY(Config, EditAnywhere, Category = "Live Blueprint Debugger")
	EPropertyRefreshRate PropertyRefreshRate = EPropertyRefreshRate::Ten;

	UPROPERTY(Config, EditAnywhere, Category = "Live Blueprint Debugger")
	FLinearColor PropertyChangedHighlightColor = FLinearColor(0.0f, 1.0f, 0.0f, 0.6f);
};
