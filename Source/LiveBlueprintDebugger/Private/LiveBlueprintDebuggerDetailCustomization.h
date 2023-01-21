#pragma once

#include "CoreMinimal.h"
#include "IDetailCustomization.h"

struct FLiveBlueprintWidgetRowData
{
	void* Container = nullptr;
	TSharedPtr<struct FPropertyInstanceInfo> PropertyInstanceInfo;
	double LastUpdateTimeInSeconds = 0.0;
	uint32 ValueHash = 0;
	TSharedPtr<class SBorder> ValueBorderWidget;
	TSharedPtr<class SHorizontalBox> ValueWidgetContainer;
};

/**
 * Adds additional detail customizations for any Actor class that also has Blueprint variables.
 */
class FLiveBlueprintDebuggerDetailCustomization
{
public:
	static TUniquePtr<FLiveBlueprintDebuggerDetailCustomization> CreateForLayoutBuilder(
		IDetailLayoutBuilder& LayoutBuilder);

private:
	static TWeakObjectPtr<AActor> GetActorToCustomize(IDetailLayoutBuilder& LayoutBuilder);
	static bool IsAnyAncestorABlueprintClass(UClass* Class);

private:
	FLiveBlueprintDebuggerDetailCustomization(TWeakObjectPtr<AActor> ActorToCustomize, IDetailLayoutBuilder& LayoutBuilder);
public:
	~FLiveBlueprintDebuggerDetailCustomization();
	
private:
	void UpdateBlueprintDetails();

	void ExpandPropertyChildren(
		class IDetailGroup& Group, 
		TSharedPtr<struct FPropertyInstanceInfo> PropertyInstanceInfo,
		void* Container,
		int LevelsOfRecursion = 0);
	
	static void FillInWidgetRow(FDetailWidgetRow& WidgetRow, FLiveBlueprintWidgetRowData& WidgetRowData, int LogIndentation = 0);
	static TSharedRef<class SWidget> GenerateNameIcon(const TSharedPtr<struct FPropertyInstanceInfo>& PropertyInstanceInfo);
	static TSharedRef<class SWidget> GenerateNameWidget(const TSharedPtr<struct FPropertyInstanceInfo>& PropertyInstanceInfo);
	static TSharedRef<class SWidget> GenerateValueWidget(const TSharedPtr<struct FPropertyInstanceInfo>& PropertyInstanceInfo);
	static void UpdateWidgetRowValue(FLiveBlueprintWidgetRowData& WidgetRowData);
	static void UpdateWidgetRow(FLiveBlueprintWidgetRowData& LiveBlueprintWidgetRow, double RealTimeInSeconds);
	static FString GetPropertyCategoryString(FProperty* Property);
	static uint32 GetPropertyValueHash(void* Container, const FProperty* Property);
	static TArray<TSharedPtr<class FDebugLineItem>> GetActorBlueprintPropertiesAsDebugTreeItemPtrs(AActor* Actor);
	static TSharedPtr<struct FPropertyInstanceInfo> GetPropertyInstanceInfo(void* Container, const FProperty* Property);
	static bool ShouldExpandProperty(const TSharedPtr<struct FPropertyInstanceInfo>& PropertyInstanceInfo);

	TWeakObjectPtr<AActor> Actor;
	TArray<FLiveBlueprintWidgetRowData> WidgetRows;
	FTimerHandle UpdateTimerHandle;
	TSharedPtr<class SKismetDebugTreeView> DebugTreeWidget;
	TSharedPtr<class FDebugLineItem> RootDebugTreeItem;
};