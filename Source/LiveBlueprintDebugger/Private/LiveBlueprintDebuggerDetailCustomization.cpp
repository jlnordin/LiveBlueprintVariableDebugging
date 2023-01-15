#include "LiveBlueprintDebuggerDetailCustomization.h"

#include "BlueprintEditor.h"
#include "DetailCategoryBuilder.h"
#include "DetailLayoutBuilder.h"
#include "DetailWidgetRow.h"
#include "IDetailGroup.h"
#include "Brushes/SlateColorBrush.h"
#include "Debugging/SKismetDebugTreeView.h"
#include "Kismet2/KismetDebugUtilities.h"
#include "LiveBlueprintDebugger.h"
#include "LiveBlueprintDebuggerSettings.h"
#include "Widgets/Text/STextBlock.h"

#define LOCTEXT_NAMESPACE "FLiveBlueprintDebuggerModule"

struct PropertyDebugItemPair
{
	TSharedPtr<FPropertyInstanceInfo> PropertyInstanceInfo;
	FDebugTreeItemPtr DebugItem;
};

static TArray<PropertyDebugItemPair> GetChildren(
	TSharedPtr<FPropertyInstanceInfo>& PropertyInstanceInfo,
	FDebugTreeItemPtr& DebugItem)
{
	TArray<PropertyDebugItemPair> Children;
	Children.Reserve(PropertyInstanceInfo->Children.Num());

	TArray<FDebugTreeItemPtr> DebugItemChildren;
	DebugItem->GatherChildrenBase(DebugItemChildren, FString());

	for (
		int32 ChildDebugIndex = 0, ChildPropertyIndex = 0;
		ChildDebugIndex < DebugItemChildren.Num() && ChildPropertyIndex < PropertyInstanceInfo->Children.Num();
		ChildDebugIndex++, ChildPropertyIndex++)
	{
		Children.Add({PropertyInstanceInfo->Children[ChildPropertyIndex], DebugItemChildren[ChildDebugIndex]});
	}

	return Children;
}

static const FString c_PrivateCategoryName = "Private Implementation Variables";
static const float c_PropertyRefreshPeriodInSeconds = 0.1f;
static const FSlateColorBrush c_HighlightedBackgroundBrush = FSlateColorBrush(FLinearColor(0.0f, 1.0f, 0.0f, 0.6f));

TUniquePtr<FLiveBlueprintDebuggerDetailCustomization> FLiveBlueprintDebuggerDetailCustomization::CreateForLayoutBuilder(
	IDetailLayoutBuilder& LayoutBuilder)
{
	TWeakObjectPtr<AActor> Actor = GetActorToCustomize(LayoutBuilder);

    if (Actor == nullptr)
    {
        return nullptr;
    }

    if (!IsAnyAncestorABlueprintClass(Actor->GetClass()))
    {
        return nullptr;
    }

	const ULiveBlueprintDebuggerSettings* Settings = GetDefault<ULiveBlueprintDebuggerSettings>();

	if ((Settings->WhenToShowVariables == EShowBlueprintVariables::OnlyWhenPlayingOrSimulating) &&
		(Actor->GetWorld()->WorldType != EWorldType::PIE))
	{
		UE_LOG(
			LogLiveBlueprintDebugger,
			Verbose,
			TEXT("Live Blueprint Variables configured to only show when playing or simulating in the editor."));

		return nullptr;
	}

	return TUniquePtr<FLiveBlueprintDebuggerDetailCustomization>(
		new FLiveBlueprintDebuggerDetailCustomization{Actor, LayoutBuilder});
}

TWeakObjectPtr<AActor> FLiveBlueprintDebuggerDetailCustomization::GetActorToCustomize(IDetailLayoutBuilder& LayoutBuilder)
{
    TArray<TWeakObjectPtr<UObject>> ObjectsBeingCustomized;
    LayoutBuilder.GetObjectsBeingCustomized(ObjectsBeingCustomized);

    if (ObjectsBeingCustomized.IsEmpty())
    {
        UE_LOG(
            LogLiveBlueprintDebugger,
            Display,
            TEXT("No objects selected."));

        return nullptr;
    }

    if (ObjectsBeingCustomized.Num() > 1)
    {
        UE_LOG(
            LogLiveBlueprintDebugger,
            Display,
            TEXT("Blueprint details only support one object."));

        return nullptr;
    }

    auto Object = ObjectsBeingCustomized[0];

    if (!Object.IsValid())
    {
        UE_LOG(
            LogLiveBlueprintDebugger,
            Display,
            TEXT("Selected object is invalid."));

        return nullptr;
    }
	
    return TWeakObjectPtr<AActor>(CastChecked<AActor>(Object));
}

bool FLiveBlueprintDebuggerDetailCustomization::IsAnyAncestorABlueprintClass(UClass* Class)
{
	while (Class != nullptr)
	{
		if (Class->ClassGeneratedBy != nullptr)
		{
			return true;
		}

		Class = Class->GetSuperClass();
	}

	return false;
}

FLiveBlueprintDebuggerDetailCustomization::FLiveBlueprintDebuggerDetailCustomization(
	TWeakObjectPtr<AActor> ActorToCustomize,
	IDetailLayoutBuilder& LayoutBuilder) :
		Actor(ActorToCustomize)
{
	UE_LOG(
		LogLiveBlueprintDebugger,
		Verbose,
		TEXT("Customizing Actor '%s'..."),
		*Actor->GetName());

	// Categorize and sort the properties associated with this Blueprint class.
	TArray<FDebugTreeItemPtr> DebugTreeItems = 
		GetActorBlueprintPropertiesAsDebugTreeItemPtrs(Actor.Get());

	TMap<FString, TArray<PropertyDebugItemPair>> PropertiesByCategory;

	int32 DebugTreeItemIndex = 0;
	TFieldIterator<FProperty> Iterator{ Actor->GetClass() };
	while ((Iterator != nullptr) && (DebugTreeItemIndex < DebugTreeItems.Num()))
	{
		FProperty* Property = *Iterator;

		if (Property->HasAllPropertyFlags(CPF_BlueprintVisible))
		{
			PropertiesByCategory.FindOrAdd(GetPropertyCategoryString(Property)).Add(
				{ 
					GetPropertyInstanceInfo(Actor.Get(), Property), 
					DebugTreeItems[DebugTreeItemIndex]
				});

			++DebugTreeItemIndex;
		}

		++Iterator;
	}

	// Add the Blueprint details section.
	FPropertyEditorModule& PropertyModule = FModuleManager::LoadModuleChecked<FPropertyEditorModule>("PropertyEditor");
	TSharedRef<FPropertySection> BlueprintSection = PropertyModule.FindOrCreateSection("Actor", "Blueprint", LOCTEXT("BlueprintSection", "Blueprint"));

	// Add widgets for all of the categories and properties.
	for (auto& [CategoryString, PropertyAndDebugItemArray] : PropertiesByCategory)
	{
		FName CategoryName = *FString::Printf(TEXT("Blueprint Properties - %s"), *CategoryString);

		if (!BlueprintSection->HasAddedCategory(CategoryName))
		{
			BlueprintSection->AddCategory(CategoryName);
		}

		IDetailCategoryBuilder& BlueprintCategory = LayoutBuilder.EditCategory(CategoryName);

		for (auto& [PropertyInstanceInfo, DebugItem] : PropertyAndDebugItemArray)
		{
			if (ShouldExpandProperty(PropertyInstanceInfo))
			{
				ExpandPropertyChildren(
					DebugItem,
					BlueprintCategory.AddGroup(
						PropertyInstanceInfo->Property->GetFName(), 
						PropertyInstanceInfo->DisplayName),
					PropertyInstanceInfo,
					Actor.Get());
			}
			else
			{
				const bool bAdvancedDisplay =
					PropertyInstanceInfo->Property->HasAnyPropertyFlags(CPF_AdvancedDisplay) ||
					(CategoryString == c_PrivateCategoryName);

				FLiveBlueprintWidgetRowData NewRowData = {
					DebugItem,
					Actor.Get(),
					PropertyInstanceInfo,
					0,
					0
				};

				FillInWidgetRow(
					BlueprintCategory.AddCustomRow(PropertyInstanceInfo->Property->GetDisplayNameText(), bAdvancedDisplay),
					NewRowData,
					0);

				WidgetRows.Add(NewRowData);
			}
		}
	}

	// Register a timer to keep our values up-to-date.
	if (Actor->GetWorld()->WorldType == EWorldType::PIE)
	{
		Actor->GetWorldTimerManager().SetTimer(
			UpdateTimerHandle,
			[this]()
			{
				UpdateBlueprintDetails();
			},
			c_PropertyRefreshPeriodInSeconds,
			true);
	}
}

FLiveBlueprintDebuggerDetailCustomization::~FLiveBlueprintDebuggerDetailCustomization()
{
	if (Actor.IsValid() && UpdateTimerHandle.IsValid())
	{
		Actor->GetWorldTimerManager().ClearTimer(UpdateTimerHandle);
	}
}

void FLiveBlueprintDebuggerDetailCustomization::UpdateBlueprintDetails()
{
	if (!Actor.IsValid())
	{
		return;
	}

	double RealTimeInSeconds = Actor->GetWorld()->GetRealTimeSeconds();

	for (auto& Row : WidgetRows)
	{
		UpdateWidgetRow(Row, RealTimeInSeconds);
	}
}

void FLiveBlueprintDebuggerDetailCustomization::ExpandPropertyChildren(
	FDebugTreeItemPtr DebugItem,
	IDetailGroup& Group,
	TSharedPtr<FPropertyInstanceInfo> PropertyInstanceInfo,
	void* Container,
	int LevelsOfRecursion)
{
	// Fill in the group's header row.
	FLiveBlueprintWidgetRowData HeaderRowData = {
		DebugItem,
		Container,
		PropertyInstanceInfo,
		0,
		0};

	FillInWidgetRow(Group.HeaderRow(), HeaderRowData, LevelsOfRecursion * 2 + 2);
	WidgetRows.Add(HeaderRowData);

	for (const auto&[ChildPropertyInfo, ChildDebugItem] : GetChildren(PropertyInstanceInfo, DebugItem))
	{
        if (ShouldExpandProperty(ChildPropertyInfo) && LevelsOfRecursion < 5)
        {
            IDetailGroup& SubGroup = Group.AddGroup(
				ChildPropertyInfo->Property->GetFName(),
				ChildPropertyInfo->DisplayName,
                false);

			ExpandPropertyChildren(
				ChildDebugItem,
				SubGroup,
				ChildPropertyInfo,
				PropertyInstanceInfo->Property->ContainerPtrToValuePtr<void>(Container),
				LevelsOfRecursion + 1);
        }
        else
        {
			FLiveBlueprintWidgetRowData NewRowData = {
				ChildDebugItem,
				PropertyInstanceInfo->Property->ContainerPtrToValuePtr<void>(Container),
				ChildPropertyInfo,
				0,
				0
			};

			FillInWidgetRow(Group.AddWidgetRow(), NewRowData, LevelsOfRecursion * 2 + 2);
			WidgetRows.Add(NewRowData);
        }
    }
}

void FLiveBlueprintDebuggerDetailCustomization::FillInWidgetRow(
	FDetailWidgetRow& WidgetRow,
	FLiveBlueprintWidgetRowData& WidgetRowData,
	int LogIndentation)
{
	WidgetRowData.ValueHash = GetPropertyValueHash(
		WidgetRowData.Container,
		*WidgetRowData.PropertyInstanceInfo->Property);

	FString Indentation = std::wstring(LogIndentation, L' ').c_str();

    UE_LOG(
        LogLiveBlueprintDebugger,
		Verbose,
        TEXT("%sProperty: '%s' [%s] \tFlags: 0x%08X\tHash: %i"),
		*Indentation,
        *WidgetRowData.PropertyInstanceInfo->Property->GetName(),
        *WidgetRowData.PropertyInstanceInfo->Property->GetClass()->GetName(),
		WidgetRowData.PropertyInstanceInfo->Property->GetPropertyFlags(),
		WidgetRowData.ValueHash);

	WidgetRow
		.NameContent()
		[
			SNew(SHorizontalBox)
			
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.HAlign(HAlign_Left)
			.VAlign(VAlign_Center)
			[
				GenerateNameIcon(WidgetRowData.PropertyInstanceInfo)
			]
			
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.HAlign(HAlign_Left)
			.VAlign(VAlign_Center)
			.Padding(5.f, 0.f, 0.f, 0.f)
			[
				WidgetRowData.DebugItem->GenerateNameWidget(MakeShared<FString>())
			]
		]
		.ValueContent()
		[
			SAssignNew(WidgetRowData.ValueBorderWidget, SBorder)
			.HAlign(EHorizontalAlignment::HAlign_Fill)
			.VAlign(EVerticalAlignment::VAlign_Fill)
			.BorderBackgroundColor(FColor::Transparent)
			.BorderImage(&c_HighlightedBackgroundBrush)
			.Content()
			[
				SAssignNew(WidgetRowData.ValueWidgetContainer, SHorizontalBox)
		
				+ SHorizontalBox::Slot()
				.AutoWidth()
				[
					WidgetRowData.DebugItem->GetValueIcon()
				]

				+ SHorizontalBox::Slot()
				.AutoWidth()
				.HAlign(HAlign_Left)
				.Padding(.5f, 1.f)
			]
		];

	UpdateWidgetRowValue(WidgetRowData);
}

TSharedRef<SWidget> FLiveBlueprintDebuggerDetailCustomization::GenerateNameIcon(
	const TSharedPtr<FPropertyInstanceInfo>& PropertyInstanceInfo)
{
	// Unreal 5.1 has a bug where calling FDebugLineItem::GetNameIcon will crash if the given 
	// property falls back to the default icon. So instead of relying on this, we create our 
	// own icon in a similar way to the Kismet / Blueprint Debugger here.
	
	FSlateColor BaseColor;
	FSlateColor UnusedColor;
	FSlateBrush const* UnusedIcon = nullptr;
	const FSlateBrush* IconBrush = FBlueprintEditor::GetVarIconAndColorFromProperty(
		PropertyInstanceInfo->Property.Get(),
		BaseColor,
		UnusedIcon,
		UnusedColor
	);

	return SNew(SImage)
		.Image(IconBrush)
		.ColorAndOpacity(BaseColor)
		.ToolTipText(PropertyInstanceInfo->Type);
}

TSharedRef<SWidget> FLiveBlueprintDebuggerDetailCustomization::GenerateValueWidget(
	const TSharedPtr<FPropertyInstanceInfo>& PropertyInstanceInfo)
{
	FText ValueText;

	if (PropertyInstanceInfo->Property->IsA<FObjectProperty>() || 
		PropertyInstanceInfo->Property->IsA<FInterfaceProperty>())
	{
		FString ValueAsString;
		if (PropertyInstanceInfo.IsValid() && (PropertyInstanceInfo->Object.Get() != nullptr))
		{
			ValueAsString = FString::Printf(
				TEXT("%s (Class: %s)"),
				*PropertyInstanceInfo->Object->GetName(),
				*PropertyInstanceInfo->Object->GetClass()->GetName());
		}
		else
		{
			ValueAsString = "None";
		}

		ValueText = FText::FromString(ValueAsString);
	}
	else
	{
		const FString ValueAsString = PropertyInstanceInfo->Value.ToString();
		ValueText = FText::FromString(ValueAsString.Replace(TEXT("\n"), TEXT(" ")));
	}

	return SNew(STextBlock)
		.Text(ValueText)
		.ToolTipText(ValueText);
}

void FLiveBlueprintDebuggerDetailCustomization::UpdateWidgetRowValue(FLiveBlueprintWidgetRowData& WidgetRowData)
{
	// We have special handling for set, array, and map properties such that their immediate children 
	// are also included in the ValueWidgetContainer. This allows the number of elements to change 
	// dynamically without needing to add a new row to the Blueprint details category, which is not 
	// feasible after it has been constructed.

	if (WidgetRowData.PropertyInstanceInfo->Property->IsA<FSetProperty>() ||
		WidgetRowData.PropertyInstanceInfo->Property->IsA<FArrayProperty>() ||
		WidgetRowData.PropertyInstanceInfo->Property->IsA<FMapProperty>())
	{
		auto VerticalBox = SNew(SVerticalBox);
		
		for (const auto& [ChildPropertyInfo, ChildDebugItem] :
			GetChildren(WidgetRowData.PropertyInstanceInfo, WidgetRowData.DebugItem))
		{
			if (WidgetRowData.PropertyInstanceInfo->Property->IsA<FMapProperty>())
			{
				VerticalBox->AddSlot()
					.AutoHeight()
					[
						SNew(SHorizontalBox)

						+ SHorizontalBox::Slot()
						.AutoWidth()
						[
							ChildDebugItem->GenerateNameWidget(MakeShared<FString>())
						]

						+ SHorizontalBox::Slot()
						.AutoWidth()
						.HAlign(HAlign_Left)
						.Padding(.5f, 1.f)
						[
							GenerateValueWidget(ChildPropertyInfo)
						]
					];
			}
			else
			{
				VerticalBox->AddSlot()
					.AutoHeight()
					[
						GenerateValueWidget(ChildPropertyInfo)
					];
			}
		}

		if (WidgetRowData.PropertyInstanceInfo->Children.IsEmpty())
		{
			VerticalBox->AddSlot()
				.AutoHeight()
				[
					SNew(STextBlock)
					.Text(FText::FromString("[empty]"))
				];
		}

		WidgetRowData.ValueWidgetContainer->GetSlot(1)
			[
				VerticalBox
			];
	}
	else
	{
		WidgetRowData.ValueWidgetContainer->GetSlot(1)
			[
				GenerateValueWidget(WidgetRowData.PropertyInstanceInfo)
			];
	}
}

void FLiveBlueprintDebuggerDetailCustomization::UpdateWidgetRow(
	FLiveBlueprintWidgetRowData& WidgetRowData,
	double RealTimeInSeconds)
{
	SBorder& Border = *WidgetRowData.ValueBorderWidget;

	uint32 NewValueHash = GetPropertyValueHash(
		WidgetRowData.Container,
		*WidgetRowData.PropertyInstanceInfo->Property);

	if (NewValueHash != WidgetRowData.ValueHash)
	{
		WidgetRowData.PropertyInstanceInfo = GetPropertyInstanceInfo(
			WidgetRowData.Container,
			*WidgetRowData.PropertyInstanceInfo->Property);

		UpdateWidgetRowValue(WidgetRowData);

		WidgetRowData.ValueHash = NewValueHash;
		WidgetRowData.LastUpdateTimeInSeconds = RealTimeInSeconds;
	}

	const ULiveBlueprintDebuggerSettings* Settings = GetDefault<ULiveBlueprintDebuggerSettings>();

	if (Settings->bHighlightValuesThatHaveChanged)
	{
		double TimeSincePropertyChanged = (RealTimeInSeconds - WidgetRowData.LastUpdateTimeInSeconds);
		if (TimeSincePropertyChanged <= 2.0)
		{
			FLinearColor BackgroundColor = FLinearColor::LerpUsingHSV(
				FLinearColor::Green,
				FLinearColor::Transparent,
				static_cast<float>(std::clamp(TimeSincePropertyChanged, 0.0, 1.0)));

			Border.SetBorderBackgroundColor(BackgroundColor);
		}
	}
}

FString FLiveBlueprintDebuggerDetailCustomization::GetPropertyCategoryString(FProperty* Property)
{
    FString Category = c_PrivateCategoryName;
    
    auto MetaDataMap = Property->GetMetaDataMap();
    
    if (MetaDataMap != nullptr &&
        MetaDataMap->Contains(FName("Category")))
    {
        Category = *(MetaDataMap->Find(FName("Category")));
		Category.ReplaceInline(TEXT("|"), TEXT(" "));
    }

    return Category;
}

uint32 FLiveBlueprintDebuggerDetailCustomization::GetPropertyValueHash(void* Container, const FProperty* Property)
{
	uint32 ValueHash = 0;

	if (Property->IsA<FStructProperty>() ||
		Property->IsA<FArrayProperty>() ||
		Property->IsA<FMapProperty>() ||
		Property->IsA<FSetProperty>())
	{
		TSharedPtr<FPropertyInstanceInfo> SetInfo = GetPropertyInstanceInfo(Container, Property);

		for (const auto& Child : SetInfo->Children)
		{
			ValueHash = HashCombineFast(
				ValueHash,
				GetPropertyValueHash(
					Property->ContainerPtrToValuePtr<void>(Container),
					*Child->Property));
		}
	}
	else if ((Property->PropertyFlags & CPF_HasGetValueTypeHash))
	{
		ValueHash = Property->GetValueTypeHash(Property->ContainerPtrToValuePtr<void>(Container));
	}

	return ValueHash;
}

TArray<FDebugTreeItemPtr> FLiveBlueprintDebuggerDetailCustomization::GetActorBlueprintPropertiesAsDebugTreeItemPtrs(AActor* Actor)
{
	TArray<FDebugTreeItemPtr> PropertyDebugTreeItems;

	FDebugTreeItemPtr RootDebugTreeItem = SKismetDebugTreeView::MakeParentItem(Actor);
	RootDebugTreeItem->GatherChildrenBase(PropertyDebugTreeItems, FString());
	FDebugTreeItemPtr SelfNode = PropertyDebugTreeItems[0];

	PropertyDebugTreeItems.Reset();
	SelfNode->GatherChildrenBase(PropertyDebugTreeItems, FString());

	return PropertyDebugTreeItems;
}

TSharedPtr<FPropertyInstanceInfo> FLiveBlueprintDebuggerDetailCustomization::GetPropertyInstanceInfo(void* Container, const FProperty* Property)
{
	TSharedPtr<FPropertyInstanceInfo> InstanceInfo;
	FKismetDebugUtilities::GetDebugInfoInternal(
		InstanceInfo,
		Property,
		Property->ContainerPtrToValuePtr<void>(Container));

	return InstanceInfo;
}

bool FLiveBlueprintDebuggerDetailCustomization::ShouldExpandProperty(const TSharedPtr<FPropertyInstanceInfo>& PropertyInstanceInfo)
{
	return (
		// We don't expand a property if it doesn't have any children.
		!PropertyInstanceInfo->Children.IsEmpty() &&

		// We are only interested in Blueprint-visible properties as well. This is how the Kismet widgets
		// filter their properties, so without requiring changes to the main Unreal Engine code, we also 
		// must filter the same way.
		PropertyInstanceInfo->Property->HasAllPropertyFlags(CPF_BlueprintVisible) &&

		// Expanding object types leads to nearly unbounded recursion for discovering all of the 
		// properties, so we filter object-type properties and UObjectWrappers out.
		!PropertyInstanceInfo->Property->HasAnyPropertyFlags(CPF_UObjectWrapper) &&
		!PropertyInstanceInfo->Property->IsA<FObjectProperty>() &&

		// We also don't expand sets, arrays, or maps because when their values change, the number of
		// children is also likely to change. To make the live updates more performant, we don't allow 
		// changing the number of widgets in the details panel. Instead we update the existing ones.
		!PropertyInstanceInfo->Property->IsA<FSetProperty>() &&
		!PropertyInstanceInfo->Property->IsA<FArrayProperty>() &&
		!PropertyInstanceInfo->Property->IsA<FMapProperty>());
}
