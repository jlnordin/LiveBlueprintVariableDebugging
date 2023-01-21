#include "LiveBlueprintDebuggerDetailCustomization.h"

#include <chrono>
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

static const FString c_PrivateCategoryName = "Private Implementation Variables";
static const FSlateColorBrush c_HighlightedBackgroundBrush = FSlateColorBrush(FLinearColor::White);

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

long long g_timeInsideGetPropertyInstanceInfoMicroSeconds = 0;

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

	g_timeInsideGetPropertyInstanceInfoMicroSeconds = 0;

	// Categorize and sort the properties associated with this Blueprint class.
	TMap<FString, TArray<TSharedPtr<FPropertyInstanceInfo>>> PropertiesByCategory;
	for (auto Iterator = TFieldIterator<FProperty>(Actor->GetClass()); Iterator != nullptr; ++Iterator)
	{
		FProperty* Property = *Iterator;

		if (Property->HasAllPropertyFlags(CPF_BlueprintVisible))
		{
			PropertiesByCategory.FindOrAdd(GetPropertyCategoryString(Property)).Add(
				{ 
					GetPropertyInstanceInfo(Actor.Get(), Property)
				});
		}
	}

	// Add the Blueprint details section.
	FPropertyEditorModule& PropertyModule = FModuleManager::LoadModuleChecked<FPropertyEditorModule>("PropertyEditor");
	TSharedRef<FPropertySection> BlueprintSection = PropertyModule.FindOrCreateSection("Actor", "Blueprint", LOCTEXT("BlueprintSection", "Blueprint"));

	// Add widgets for all of the categories and properties.
	for (auto& [CategoryString, PropertyInfoArray] : PropertiesByCategory)
	{
		FName CategoryName = *FString::Printf(TEXT("Blueprint Properties - %s"), *CategoryString);

		if (!BlueprintSection->HasAddedCategory(CategoryName))
		{
			BlueprintSection->AddCategory(CategoryName);
		}

		IDetailCategoryBuilder& BlueprintCategory = LayoutBuilder.EditCategory(CategoryName);
		
		for (auto& PropertyInstanceInfo : PropertyInfoArray)
		{
			if (ShouldExpandProperty(PropertyInstanceInfo))
			{
				ExpandPropertyChildren(
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
					Actor.Get(),
					PropertyInstanceInfo,
					0,
					0,
					{},
					{}
				};

				FillInWidgetRow(
					BlueprintCategory.AddCustomRow(PropertyInstanceInfo->Property->GetDisplayNameText(), bAdvancedDisplay),
					NewRowData,
					0);

				WidgetRows.Add(NewRowData);
			}
		}
	}

	const ULiveBlueprintDebuggerSettings* Settings = GetDefault<ULiveBlueprintDebuggerSettings>();

	// Register a timer to keep our values up-to-date.
	if (Actor->GetWorld()->WorldType == EWorldType::PIE &&
		Settings->PropertyRefreshRate != EPropertyRefreshRate::NoLiveUpdates)
	{
		float RefreshPeriod = 1.0f;

		switch (Settings->PropertyRefreshRate)
		{
			default:
			case EPropertyRefreshRate::One:
			{
				RefreshPeriod = 1.0f;
				break;
			}

			case EPropertyRefreshRate::Ten:
			{
				RefreshPeriod = 0.1f;
				break;
			}

			case EPropertyRefreshRate::Thirty:
			{
				RefreshPeriod = 0.0334f;
				break;
			}
		}

		Actor->GetWorldTimerManager().SetTimer(
			UpdateTimerHandle,
			[this]()
			{
				UpdateBlueprintDetails();
			},
			RefreshPeriod,
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
	IDetailGroup& Group,
	TSharedPtr<FPropertyInstanceInfo> PropertyInstanceInfo,
	void* Container,
	int LevelsOfRecursion)
{
	// Fill in the group's header row.
	FLiveBlueprintWidgetRowData HeaderRowData = {
		Container,
		PropertyInstanceInfo,
		0,
		0,
		{},
		{}
	};

	FillInWidgetRow(Group.HeaderRow(), HeaderRowData, LevelsOfRecursion * 2 + 2);
	WidgetRows.Add(HeaderRowData);

	for (const auto& ChildPropertyInfo : PropertyInstanceInfo->Children)
	{
        if (ShouldExpandProperty(ChildPropertyInfo) && LevelsOfRecursion < 5)
        {
            IDetailGroup& SubGroup = Group.AddGroup(
				ChildPropertyInfo->Property->GetFName(),
				ChildPropertyInfo->DisplayName,
                false);

			ExpandPropertyChildren(
				SubGroup,
				ChildPropertyInfo,
				PropertyInstanceInfo->Property->ContainerPtrToValuePtr<void>(Container),
				LevelsOfRecursion + 1);
        }
        else
        {
			FLiveBlueprintWidgetRowData NewRowData = {
				PropertyInstanceInfo->Property->ContainerPtrToValuePtr<void>(Container),
				ChildPropertyInfo,
				0,
				0,
				{},
				{}
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
				GenerateNameWidget(WidgetRowData.PropertyInstanceInfo)
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

TSharedRef<SWidget> FLiveBlueprintDebuggerDetailCustomization::GenerateNameWidget(
	const TSharedPtr<FPropertyInstanceInfo>& PropertyInstanceInfo)
{
	return SNew(STextBlock)
		.Text(PropertyInstanceInfo->DisplayName)
		.ToolTipText(PropertyInstanceInfo->DisplayName);
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
		
		for (const auto& ChildPropertyInfo : WidgetRowData.PropertyInstanceInfo->Children)
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
							GenerateNameWidget(ChildPropertyInfo)
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

		WidgetRowData.ValueWidgetContainer->GetSlot(0)
			[
				VerticalBox
			];
	}
	else
	{
		WidgetRowData.ValueWidgetContainer->GetSlot(0)
			[
				GenerateValueWidget(WidgetRowData.PropertyInstanceInfo)
			];
	}
}

void FLiveBlueprintDebuggerDetailCustomization::UpdateWidgetRow(
	FLiveBlueprintWidgetRowData& WidgetRowData,
	double RealTimeInSeconds)
{
	if (*WidgetRowData.PropertyInstanceInfo->Property == nullptr)
	{
		return;
	}

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
			FLinearColor BackgroundColor = Settings->PropertyChangedHighlightColor;
			BackgroundColor.A = BackgroundColor.A * (1.0f - static_cast<float>(std::clamp(TimeSincePropertyChanged, 0.0, 1.0)));
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

		void* ChildContainer = Property->ContainerPtrToValuePtr<void>(Container);

		for (const auto& Child : SetInfo->Children)
		{
			const FProperty* ChildProperty = *Child->Property;

			if (!ChildProperty->IsA<FStructProperty>() &&
				(ChildProperty->PropertyFlags & CPF_HasGetValueTypeHash))
			{
				ValueHash = HashCombineFast(
					ValueHash, 
					ChildProperty->GetValueTypeHash(ChildProperty->ContainerPtrToValuePtr<void>(ChildContainer)));
			}
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
	auto start = std::chrono::high_resolution_clock::now();

	TSharedPtr<FPropertyInstanceInfo> InstanceInfo;
	FKismetDebugUtilities::GetDebugInfoInternal(
		InstanceInfo,
		Property,
		Property->ContainerPtrToValuePtr<void>(Container));

	g_timeInsideGetPropertyInstanceInfoMicroSeconds +=
		std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::high_resolution_clock::now() - start).count();

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
