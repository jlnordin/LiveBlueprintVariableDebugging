// Copyright (c) 2022-2023 Justin Nordin. All Rights Reserved.

#include "FastPropertyInstanceInfo.h"

#include "LiveBlueprintDebugger.h"

#include "Kismet2/KismetDebugUtilities.h"

#include <string_view>

#define LOCTEXT_NAMESPACE "FLiveBlueprintDebuggerModule"

FFastPropertyInstanceInfo::FFastPropertyInstanceInfo(
	void* Container, 
	const FProperty* Property) :
		ValuePointer(Property->ContainerPtrToValuePtr<void>(Container)),
		Property(Property)
{
	PopulateChildren();
	PopulateObject();
	PopulateText();
}

FFastPropertyInstanceInfo::FFastPropertyInstanceInfo(
	void* ValuePointer,
	TSharedPtr<FPropertyInstanceInfo>& PropertyInstanceInfo) :
		ValuePointer(ValuePointer),
		Property(PropertyInstanceInfo->Property),
		DisplayNameText(PropertyInstanceInfo->DisplayName),
		ValueText(PropertyInstanceInfo->Value),
		TypeText(PropertyInstanceInfo->Type),
		Object(PropertyInstanceInfo->Object)
{

}

const TFieldPath<const FProperty>& FFastPropertyInstanceInfo::GetProperty() const
{
	return Property;
}

FText FFastPropertyInstanceInfo::GetDisplayName() const
{
	return DisplayNameText;
}

FText FFastPropertyInstanceInfo::GetType() const
{
	return TypeText;
}

FText FFastPropertyInstanceInfo::GetValue() const
{
	return ValueText;
}

void* FFastPropertyInstanceInfo::GetValuePointer() const
{
	return ValuePointer;
}

uint32 FFastPropertyInstanceInfo::GetValueHash() const
{
	uint32 ValueHash = 0;

	// For array, map, and set properties, seed the hash with the number of elements. This will cause 
	// the hash to change if the number of elements change without needing to reinitialize all of the
	// child elements.
	if (auto ArrayProperty = CastField<FArrayProperty>(Property.Get()); ArrayProperty != nullptr)
	{
		FScriptArrayHelper ArrayHelper{ ArrayProperty, ValuePointer };
		ValueHash = ArrayHelper.Num();
	}
	else if (auto MapProperty = CastField<FMapProperty>(Property.Get()); MapProperty != nullptr)
	{
		FScriptMapHelper MapHelper{ MapProperty, ValuePointer };
		ValueHash = MapHelper.Num();
	}
	else if (auto SetProperty = CastField<FSetProperty>(Property.Get()); SetProperty != nullptr)
	{
		FScriptSetHelper SetHelper{ SetProperty, ValuePointer };
		ValueHash = SetHelper.Num();
	}

	if (Property->IsA<FStructProperty>() ||
		Property->IsA<FArrayProperty>() ||
		Property->IsA<FMapProperty>() ||
		Property->IsA<FSetProperty>())
	{
		for (auto& Child : Children)
		{
			ValueHash = HashCombineFast(
				ValueHash,
				Child.GetValueHash());
		}
	}
	else if (Property->IsA<FInterfaceProperty>())
	{
		// FInterfaceProperty doesn't define a GetValueTypeHashInternal override function, so we instead
		// use the hash of its value string.

		FString ValueString = ValueText.ToString();
		std::hash<std::wstring_view> Hasher;
		ValueHash = Hasher({ *ValueString, static_cast<size_t>(ValueString.Len()) });
	}
	else if ((Property->PropertyFlags & CPF_HasGetValueTypeHash))
	{
		ValueHash = Property->GetValueTypeHash(ValuePointer);
	}

	return ValueHash;
}

TArray<FFastPropertyInstanceInfo>& FFastPropertyInstanceInfo::GetChildren()
{
	return Children;
}

bool FFastPropertyInstanceInfo::IsValid() const
{
	return true;
}

const TWeakObjectPtr<UObject>& FFastPropertyInstanceInfo::GetObject() const
{
	return Object;
}

void FFastPropertyInstanceInfo::Refresh()
{
	PopulateChildren();
	PopulateObject();
	PopulateText();
}

bool FFastPropertyInstanceInfo::ShouldExpandProperty(FFastPropertyInstanceInfo& PropertyInstanceInfo)
{
	return (
		// We only support expanding struct properties in the details panel UI.
		PropertyInstanceInfo.GetProperty()->IsA<FStructProperty>() &&
		PropertyInstanceInfo.GetProperty()->HasAllPropertyFlags(CPF_BlueprintVisible));
}

void FFastPropertyInstanceInfo::PopulateObject()
{
	if (auto ObjectPropertyBase = CastField<FObjectPropertyBase>(*Property); ObjectPropertyBase != nullptr)
	{
		Object = ObjectPropertyBase->GetObjectPropertyValue(ValuePointer);
	}
	else if (auto InterfaceProperty = CastField<FInterfaceProperty>(*Property); InterfaceProperty != nullptr)
	{
		auto InterfaceData = StaticCast<const FScriptInterface*>(ValuePointer);
		Object = InterfaceData->GetObject();
	}
}

FText FFastPropertyInstanceInfo::GetValueTextOfAllChildren()
{
	FString ValueTextBuilder = L"{";

	ValueTextBuilder += FString::JoinBy(
		Children,
		TEXT(", "),
		[](auto& Child)
		{
			return FString::Format(
				TEXT("{0}: {1}"),
				{
					Child.GetDisplayName().ToString(),
					Child.GetValue().ToString()
				});
		});

	ValueTextBuilder += L"}";

	return FText::FromString(ValueTextBuilder);
}

void FFastPropertyInstanceInfo::PopulateText()
{
	DisplayNameText = Property->GetDisplayNameText();
	TypeText = Property->GetClass()->GetDisplayNameText();

	if (Property->IsA<FObjectPropertyBase>() || 
		Property->IsA<FInterfaceProperty>())
	{
		if (Object.IsValid())
		{
			ValueText = FText::FromString(Object->GetFullName());
			TypeText = Object->GetClass()->GetDisplayNameText();
		}
		else
		{
			ValueText = FText::FromString(L"None");
		}
	}
	else if (auto StructProperty = CastField<FStructProperty>(*Property); StructProperty != nullptr)
	{
		ValueText = GetValueTextOfAllChildren();
		TypeText = StructProperty->Struct->GetDisplayNameText();
	}
	else if (auto ArrayProperty = CastField<FArrayProperty>(*Property); ArrayProperty != nullptr)
	{
		ValueText = GetValueTextOfAllChildren();

		if (ArrayProperty->Inner != nullptr)
		{
			TypeText = FText::FromString(
				FString::Format(TEXT("Array of {0}"), { ArrayProperty->Inner->GetClass()->GetName() }));
		}
	}
	else if (auto MapProperty = CastField<FMapProperty>(*Property); MapProperty != nullptr)
	{
		ValueText = GetValueTextOfAllChildren();

		if (MapProperty->KeyProp != nullptr &&
			MapProperty->ValueProp != nullptr)
		{
			TypeText = FText::FromString(
				FString::Format(TEXT("Map of {0} to {1}"), 
					{ 
						MapProperty->KeyProp->GetClass()->GetName(),
						MapProperty->ValueProp->GetClass()->GetName()
					}));
		}
	}
	else if (auto SetProperty = CastField<FSetProperty>(*Property); SetProperty != nullptr)
	{
		ValueText = GetValueTextOfAllChildren();

		if (SetProperty->ElementProp != nullptr)
		{
			TypeText = FText::FromString(
				FString::Format(TEXT("Array of {0}"), { SetProperty->ElementProp->GetClass()->GetName() }));
		}
	}
	else
	{
		// Here we _do_ make use of FKismetDebugUtilities::GetDebugInfoInternal to get the 
		// information we need because we know this is _not_ an unbounded property type that might have
		// hundreds of nested references. That is, this is not an object, interface, or struct.
		
		TSharedPtr<FPropertyInstanceInfo> InstanceInfo;
		FKismetDebugUtilities::GetDebugInfoInternal(
			InstanceInfo,
			*Property,
			ValuePointer);

		DisplayNameText = InstanceInfo->DisplayName;
		ValueText = InstanceInfo->Value;
		TypeText = InstanceInfo->Type;
	}
}

void FFastPropertyInstanceInfo::PopulateChildren()
{
	Children.Empty();

	if (const FStructProperty* StructProperty = CastField<FStructProperty>(Property.Get()); StructProperty != nullptr)
	{
		for (TFieldIterator<FProperty> It(StructProperty->Struct); It != nullptr; ++It)
		{
			Children.Add(FFastPropertyInstanceInfo{
				ValuePointer,
				*It });
		}
	}
	else if (
		Property->IsA<FSetProperty>() ||
		Property->IsA<FArrayProperty>() ||
		Property->IsA<FMapProperty>())
	{
		TSharedPtr<FPropertyInstanceInfo> InstanceInfo;
		FKismetDebugUtilities::GetDebugInfoInternal(
			InstanceInfo,
			*Property,
			ValuePointer);

		for (auto& Child : InstanceInfo->Children)
		{
			Children.Add(FFastPropertyInstanceInfo{ Child->Property->ContainerPtrToValuePtr<void>(ValuePointer), Child });
		}
	}
}