#include "Character/Appearance/VBCharacterAppearance.h"

#include "Components/SkeletalMeshComponent.h"
#include "GameFramework/Actor.h"
#if WITH_EDITOR
#include "Character/Appearance/VBAppearancePiece.h"
#include "Engine/BlueprintGeneratedClass.h"
#include "Animation/AnimInstance.h"
#include "Animation/Skeleton.h"
#include "Engine/InheritableComponentHandler.h"
#include "Engine/SCS_Node.h"
#include "Engine/SimpleConstructionScript.h"
#include "Engine/SkeletalMesh.h"
#include "Misc/DataValidation.h"
#endif

#define LOCTEXT_NAMESPACE "VBCharacterAppearance"

#if WITH_EDITOR
namespace
{
	// 겉모습 클래스에서 이름이 Name 인 스켈레탈 메시 컴포넌트의 실제 템플릿. 자식 BP 의 덮어쓴 값(상속 핸들러)이 있으면 가장 아래 자식이 이긴다.
	// 부모 SCS 노드의 템플릿만 보면 자식이 바꾼 바디 메시를 놓친다.
	const USkeletalMeshComponent* FindEffectiveBodyTemplate(const UClass* VisualClass, FName Name)
	{
		if (const AActor* CDO = VisualClass->GetDefaultObject<AActor>())
		{
			TArray<const USkeletalMeshComponent*> NativeMeshes;
			CDO->GetComponents(NativeMeshes);
			for (const USkeletalMeshComponent* Mesh : NativeMeshes)
			{
				if (Mesh->GetFName() == Name)
				{
					return Mesh;
				}
			}
		}

		TArray<const UBlueprintGeneratedClass*> Chain; // [0] = 가장 아래 자식
		for (const UClass* It = VisualClass; It; It = It->GetSuperClass())
		{
			if (const UBlueprintGeneratedClass* BPClass = Cast<UBlueprintGeneratedClass>(It))
			{
				Chain.Add(BPClass);
			}
		}

		for (int32 Owner = 0; Owner < Chain.Num(); ++Owner)
		{
			if (!Chain[Owner]->SimpleConstructionScript)
			{
				continue;
			}

			for (const USCS_Node* Node : Chain[Owner]->SimpleConstructionScript->GetAllNodes())
			{
				if (!Node || Node->GetVariableName() != Name)
				{
					continue;
				}

				for (int32 Child = 0; Child < Owner; ++Child)
				{
					// 읽기만 한다 - bCreateIfNecessary=false 라 클래스를 바꾸지 않는다.
					UInheritableComponentHandler* Handler =
						const_cast<UBlueprintGeneratedClass*>(Chain[Child])->GetInheritableComponentHandler(false);
					if (const USkeletalMeshComponent* Override = Handler
						? Cast<USkeletalMeshComponent>(Handler->GetOverridenComponentTemplate(FComponentKey(Node))) : nullptr)
					{
						return Override;
					}
				}

				return Cast<USkeletalMeshComponent>(Node->ComponentTemplate);
			}
		}

		return nullptr;
	}

	// 겉모습 클래스에서 스켈레탈 메시 이름, 복제 여부, 주 바디 메시의 애님과 태그 유무를 모은다.
	void CollectVisualClassFacts(const UClass* VisualClass, FName PrimaryBody, TArray<FName>& MeshNames, bool& bReplicates,
		bool& bBodyHasAnimOrTag)
	{
		const auto NoteBody = [&](FName Name, const USkeletalMeshComponent* /*Template*/)
		{
			MeshNames.Add(Name);
		};

		const AActor* CDO = VisualClass->GetDefaultObject<AActor>();
		bReplicates = CDO && CDO->GetIsReplicated();
		// 네이티브 기본 하위 객체와 BP 의 SCS 노드 둘 다 본다 - 겉모습 액터는 대개 BP 다(Kellan, Manny).
		TArray<const USkeletalMeshComponent*> NativeMeshes;
		if (CDO)
		{
			CDO->GetComponents(NativeMeshes);
		}

		for (const USkeletalMeshComponent* Mesh : NativeMeshes)
		{
			NoteBody(Mesh->GetFName(), Mesh);
		}

		for (const UClass* It = VisualClass; It; It = It->GetSuperClass())
		{
			const UBlueprintGeneratedClass* BPClass = Cast<UBlueprintGeneratedClass>(It);
			if (!BPClass || !BPClass->SimpleConstructionScript)
			{
				continue;
			}

			for (const USCS_Node* Node : BPClass->SimpleConstructionScript->GetAllNodes())
			{
				if (Node && Node->ComponentTemplate && Node->ComponentTemplate->IsA<USkeletalMeshComponent>())
				{
					NoteBody(Node->GetVariableName(), Cast<USkeletalMeshComponent>(Node->ComponentTemplate));
				}
			}
		}

		// 리타겟 담당 검사도 실제 바디 메시 템플릿으로 본다 - 자식 BP 가 부모의 바디 컴포넌트에 애님, 태그를 덮어쓸 수 있다.
		if (const USkeletalMeshComponent* EffectiveBody = FindEffectiveBodyTemplate(VisualClass, PrimaryBody))
		{
			bBodyHasAnimOrTag = EffectiveBody->AnimClass != nullptr || EffectiveBody->ComponentTags.Num() > 0;
		}
	}
}
#endif

VBAppearanceRules::EClassCheck VBAppearanceRules::CheckVisualClass(bool bHasClass, bool bClassReplicates,
	const TArray<FName>& SkeletalMeshComponentNames, FName PrimaryBody)
{
	if (!bHasClass)
	{
		return EClassCheck::NoClass;
	}

	if (bClassReplicates)
	{
		return EClassCheck::Replicated;
	}

	if (PrimaryBody.IsNone() || !SkeletalMeshComponentNames.Contains(PrimaryBody))
	{
		return EClassCheck::NoPrimaryBody;
	}

	return EClassCheck::Ok;
}

VBAppearanceRules::ERetargetCheck VBAppearanceRules::CheckRetargetOwner(bool bDataAnim, bool bDataTag, bool bBodyHasAnimOrTag)
{
	if (bDataAnim != bDataTag)
	{
		return ERetargetCheck::HalfSet;
	}

	if (bDataAnim && bBodyHasAnimOrTag)
	{
		return ERetargetCheck::TwoOwners;
	}

	return ERetargetCheck::Ok;
}

FName UVBCharacterAppearance::ResolveSocket(FName DriverSocket) const
{
	const FName* Alias = SocketAliases.Find(DriverSocket);
	return Alias ? *Alias : DriverSocket;
}

FPrimaryAssetId UVBCharacterAppearance::GetPrimaryAssetId() const
{
	return FPrimaryAssetId(GetAppearanceAssetType(), GetFName());
}

#if WITH_EDITOR
EDataValidationResult UVBCharacterAppearance::IsDataValid(FDataValidationContext& Context) const
{
	EDataValidationResult Result = Super::IsDataValid(Context);

	const UClass* VisualClass = VisualActorClass.LoadSynchronous();
	TArray<FName> MeshNames;
	bool bReplicates = false;
	bool bBodyHasAnimOrTag = false;
	if (VisualClass)
	{
		CollectVisualClassFacts(VisualClass, PrimaryBodyComponent, MeshNames, bReplicates, bBodyHasAnimOrTag);
	}

	ValidateVisualClass(VisualClass != nullptr, bReplicates, MeshNames, Context, Result);
	ValidateRetargetOwner(bBodyHasAnimOrTag, Context, Result);
	ValidateDefaultPieces(VisualClass, Context, Result);
	return Result;
}

void UVBCharacterAppearance::ValidateVisualClass(bool bHasClass, bool bReplicates, const TArray<FName>& MeshNames,
	FDataValidationContext& Context, EDataValidationResult& Result) const
{
	switch (VBAppearanceRules::CheckVisualClass(bHasClass, bReplicates, MeshNames, PrimaryBodyComponent))
	{
	case VBAppearanceRules::EClassCheck::NoClass:
		Context.AddError(LOCTEXT("NoClass", "VisualActorClass 가 비었거나 로드되지 않는다 - 본체만 쓰려면 겉모습 자산을 고르지 말 것."));
		Result = EDataValidationResult::Invalid;
		break;
	case VBAppearanceRules::EClassCheck::Replicated:
		Context.AddError(LOCTEXT("Replicated", "겉모습 액터가 복제 클래스다 - 클라이언트에서 안 만들어진다(UChildActorComponent). bReplicates 를 끌 것."));
		Result = EDataValidationResult::Invalid;
		break;
	case VBAppearanceRules::EClassCheck::NoPrimaryBody:
		Context.AddError(FText::Format(LOCTEXT("NoBody", "PrimaryBodyComponent '{0}' 가 겉모습 클래스의 스켈레탈 메시 이름이 아니다."),
			FText::FromName(PrimaryBodyComponent)));
		Result = EDataValidationResult::Invalid;
		break;
	default:
		break;
	}
}

void UVBCharacterAppearance::ValidateRetargetOwner(bool bBodyHasAnimOrTag, FDataValidationContext& Context,
	EDataValidationResult& Result) const
{
	switch (VBAppearanceRules::CheckRetargetOwner(!RetargetAnimClass.IsNull(), !RetargeterTag.IsNone(), bBodyHasAnimOrTag))
	{
	case VBAppearanceRules::ERetargetCheck::HalfSet:
		Context.AddError(LOCTEXT("RetargetHalf", "RetargetAnimClass 와 RetargeterTag 는 둘 다 채우거나 둘 다 비운다."));
		Result = EDataValidationResult::Invalid;
		break;
	case VBAppearanceRules::ERetargetCheck::TwoOwners:
		Context.AddError(LOCTEXT("RetargetTwoOwners", "겉모습 BP 의 주 몸 메시가 이미 애님이나 태그를 들고 있다 - 리타겟의 주인을 하나로(이 자산의 두 칸을 비울 것)."));
		Result = EDataValidationResult::Invalid;
		break;
	default:
		break;
	}
}

void UVBCharacterAppearance::ValidateDefaultPieces(const UClass* VisualClass, FDataValidationContext& Context,
	EDataValidationResult& Result) const
{
	// 외형 칸(GAS G6b, 계약 v1 의 V1-V5). 칸 없는 겉모습은 여기서 끝.
	const auto Fail = [&](const FText& Problem)
	{
		Context.AddError(Problem);
		Result = EDataValidationResult::Invalid;
	};

	if (CharacterId.IsNone())
	{
		if (!DefaultPieces.IsEmpty())
		{
			Fail(LOCTEXT("PiecesWithoutId", "CharacterId 가 비었는데 기본 조각이 있다 - 조각은 이 이름으로 메시를 찾는다."));
		}

		return;
	}

	const USkeletalMeshComponent* BodyTemplate = VisualClass ? FindEffectiveBodyTemplate(VisualClass, PrimaryBodyComponent) : nullptr;
	const USkeletalMesh* Body = BodyTemplate ? BodyTemplate->GetSkeletalMeshAsset() : nullptr;
	if (!Body)
	{
		Fail(LOCTEXT("NoBodyMesh", "칸 있는 겉모습인데 주 몸 컴포넌트에 메시가 없다 - 맨몸 메시를 주 몸에 둔다."));
		return;
	}

	ValidateFittedPieces(*Body, Fail);
}

void UVBCharacterAppearance::ValidateFittedPieces(const USkeletalMesh& Body, TFunctionRef<void(const FText&)> Fail) const
{
	for (const FText& Problem : VBAppearancePieceRules::FindBodySectionProblems(VBAppearancePieceRules::SectionSlotNamesPerLod(Body)))
	{
		Fail(Problem); // V3
	}

	TArray<VBAppearancePieceRules::FPieceFacts> Facts;
	for (int32 Index = 0; Index < DefaultPieces.Num(); ++Index)
	{
		const UVBAppearancePiece* Piece = DefaultPieces[Index].LoadSynchronous();
		if (!Piece)
		{
			Fail(FText::Format(LOCTEXT("NoPiece", "기본 조각 {0}번이 비었거나 읽히지 않는다."), Index));
			continue;
		}

		const USkeletalMesh* Fitted = Piece->FindFittedMesh(CharacterId).LoadSynchronous();
		if (!Fitted)
		{
			// V1: 이 캐릭터 메시가 없거나 경로가 풀리지 않는다.
			Fail(FText::Format(LOCTEXT("NoFitted", "기본 조각 {0} 에 '{1}' 의 메시가 없거나 읽히지 않는다."), FText::FromString(Piece->GetName()),
				FText::FromName(CharacterId)));
		}
		else if (Fitted->GetSkeleton() != Body.GetSkeleton())
		{
			// V2: 리더 포즈는 같은 뼈를 따른다.
			Fail(FText::Format(LOCTEXT("OtherSkeleton", "기본 조각 {0} 의 메시 스켈레톤({1})이 몸({2})과 다르다."), FText::FromString(Piece->GetName()),
				FText::FromString(GetNameSafe(Fitted->GetSkeleton())), FText::FromString(GetNameSafe(Body.GetSkeleton()))));
		}

		Facts.Add({Piece->GetName(), Piece->AppearanceSlot, Piece->HidesSections});
	}

	for (const FText& Problem : VBAppearancePieceRules::FindDefaultPieceProblems(Facts))
	{
		Fail(Problem); // V4, V5
	}
}
#endif

#undef LOCTEXT_NAMESPACE
