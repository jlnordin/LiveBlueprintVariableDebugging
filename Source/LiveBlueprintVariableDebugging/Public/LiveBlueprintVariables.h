// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Logging/LogMacros.h"
#include "Modules/ModuleManager.h"

class FToolBarBuilder;
class FMenuBuilder;

DECLARE_LOG_CATEGORY_EXTERN(LogLiveBlueprintVariables, Display, All);

class FLiveBlueprintVariablesModule : public IModuleInterface
{
public:

	/** IModuleInterface implementation */
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;

private:
	FDelegateHandle DetailCustomizationDelegateHandle;
	TUniquePtr<class FLiveBlueprintVariablesDetailCustomization> CurrentDetailCustomization;
};
