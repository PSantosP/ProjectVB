#pragma once

#include "CoreMinimal.h"
#include "UObject/UnrealType.h"

// 가져온 Chooser 의 BP 문맥 형을 리플렉션으로 읽는 뷰. 판단은 모두 네이티브 코드가 한다.
// ProcessEvent, BP 함수 실행, 공유 캐시를 쓰지 않는다.
namespace VBGASP
{
struct FStateView
{
	const UStruct* Type = nullptr;
	void* Data = nullptr;
	explicit FStateView(UObject* Object) : Type(Object ? Object->GetClass() : nullptr), Data(Object) {}
	FStateView(const UStruct* InType, void* InData) : Type(InType), Data(InData) {}
	FProperty* Find(FName Name) const
	{
		if (!Type || !Data)
		{
			return nullptr;
		}

		if (FProperty* Exact = Type->FindPropertyByName(Name))
		{
			return Exact;
		}

		const FString Prefix = Name.ToString() + TEXT("_");
		for (TFieldIterator<FProperty> It(Type); It; ++It)
		{
			if (It->GetName().StartsWith(Prefix))
			{
				return *It;
			}
		}

		return nullptr;
	}

	FStateView Child(FName Name) const
	{
		const auto* P = CastField<FStructProperty>(Find(Name));
		return FStateView(P ? P->Struct : nullptr, P ? P->ContainerPtrToValuePtr<void>(Data) : nullptr);
	}

	bool Bool(FName Name) const
	{
		const auto* P = CastField<FBoolProperty>(Find(Name));
		return P && P->GetPropertyValue_InContainer(Data);
	}

	double Number(FName Name) const
	{
		const auto* P = CastField<FNumericProperty>(Find(Name));
		if (!P)
		{
			return 0.0;
		}

		const void* Address = P->ContainerPtrToValuePtr<void>(Data);
		return P->IsFloatingPoint() ? P->GetFloatingPointPropertyValue(Address) : P->GetSignedIntPropertyValue(Address);
	}

	bool Is(FName Name, const TCHAR* AuthoredValue) const
	{
		const auto* P = CastField<FByteProperty>(Find(Name));
		return P && P->Enum && P->Enum->GetAuthoredNameStringByValue(P->GetPropertyValue_InContainer(Data)) == AuthoredValue;
	}

	template<typename T> T Struct(FName Name) const
	{
		const auto* P = CastField<FStructProperty>(Find(Name));
		return P && P->Struct == TBaseStructure<T>::Get() ? *P->ContainerPtrToValuePtr<T>(Data) : T{};
	}

	template<typename T> T NativeStruct(FName Name) const
	{
		const T* Native = NativePtr<T>(Name);
		return Native ? *Native : T{};
	}
	// 그래프 변수 안의 C++ 구조체 포인터(형이 같을 때만, 아니면 nullptr).
	template<typename T> T* NativePtr(FName Name) const
	{
		const auto* P = CastField<FStructProperty>(Find(Name));
		return P && P->Struct == T::StaticStruct() ? P->ContainerPtrToValuePtr<T>(Data) : nullptr;
	}

	bool HasName(FName Name, FName Value) const
	{
		const auto* P = CastField<FArrayProperty>(Find(Name));
		if (!P || !CastField<FNameProperty>(P->Inner))
		{
			return false;
		}

		return P->ContainerPtrToValuePtr<TArray<FName>>(Data)->Contains(Value);
	}
};
}
