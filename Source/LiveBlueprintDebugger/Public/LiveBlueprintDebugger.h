// Copyright (c) 2022-2023 Justin Nordin. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Logging/LogMacros.h"
#include "Modules/ModuleManager.h"

class FToolBarBuilder;
class FMenuBuilder;

DECLARE_LOG_CATEGORY_EXTERN(LogLiveBlueprintDebugger, Display, All);

class FLiveBlueprintDebuggerModule : public IModuleInterface
{
public:

	/** IModuleInterface implementation */
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;

private:
	FDelegateHandle DetailCustomizationDelegateHandle;
	TUniquePtr<class FLiveBlueprintDebuggerDetailCustomization> CurrentDetailCustomization;
};
