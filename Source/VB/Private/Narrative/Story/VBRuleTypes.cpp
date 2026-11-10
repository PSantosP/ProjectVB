// ProjectVB Copyright (c) 2025. All Rights Reserved.

#include "Narrative/Story/VBRuleTypes.h"

#include "Engine/World.h"
#include "Narrative/Story/VBStoryStateComponent.h"
#include "VB/VB.h"

#if WITH_EDITOR
#include "Misc/DataValidation.h"
#endif

bool VBRules::Compare(EVBRuleCompare Op, double Lhs, double Rhs)
{
	switch (Op)
	{
	case EVBRuleCompare::AtLeast: return Lhs >= Rhs;
	case EVBRuleCompare::AtMost: return Lhs <= Rhs;
	case EVBRuleCompare::Equal: return Lhs == Rhs;
	}

	return false;
}

bool FVBRuleContext::CanApplyEffects() const
{
	return !World || World->GetNetMode() != NM_Client;
}

FVBRuleContext FVBRuleContext::Make(const UObject* WorldContextObject, APlayerState* InInstigator)
{
	FVBRuleContext Context;
	Context.World = WorldContextObject ? WorldContextObject->GetWorld() : nullptr;
	Context.Instigator = InInstigator;
	Context.Story = UVBStoryStateComponent::Get(WorldContextObject);
	return Context;
}

// ---------------------------------------------------------------- 조건

bool FVBCondition_Fact::Evaluate(const FVBRuleContext& Context) const
{
	if (!Context.Story)
	{
		// 사실을 읽을 곳이 없으면 거짓이다 - 알 수 없는 것을 참으로 흘리면 잠긴 선택지가 열린다.
		VB_LOG(Warning, "조건(사실 %s): 문맥에 사실 저장소가 없다 - 거짓으로 친다", *Fact.ToString());
		return false;
	}

	return Context.Story->HasFact(Fact) != bNegate;
}

bool FVBCondition_Counter::Evaluate(const FVBRuleContext& Context) const
{
	if (!Context.Story)
	{
		VB_LOG(Warning, "조건(카운터 %s): 문맥에 사실 저장소가 없다 - 거짓으로 친다", *Counter.ToString());
		return false;
	}

	return VBRules::Compare(Compare, Context.Story->GetCounter(Counter), Value);
}

bool FVBCondition_FactCount::Evaluate(const FVBRuleContext& Context) const
{
	// 실행 때도 다시 본다 - 검증을 거치지 않은 자산(코드로 만든 줄, 검증 전 저장)이 틀린 채로 참이 되지 않게.
	const FString Problem = GetDataProblem();
	if (!Problem.IsEmpty())
	{
		VB_LOG(Warning, "조건(사실 개수): %s - 거짓으로 친다", *Problem);
		return false;
	}

	if (!Context.Story)
	{
		VB_LOG(Warning, "조건(사실 개수 %d개): 문맥에 사실 저장소가 없다 - 거짓으로 친다", Facts.Num());
		return false;
	}

	int32 On = 0;
	for (const FGameplayTag& Fact : Facts)
	{
		On += Context.Story->HasFact(Fact) ? 1 : 0;
	}

	return VBRules::Compare(Compare, On, Value);
}

FString FVBCondition_FactCount::GetDataProblem() const
{
	if (Facts.IsEmpty())
	{
		return TEXT("사실 목록이 비었다");
	}

	for (int32 Index = 0; Index < Facts.Num(); ++Index)
	{
		if (!Facts[Index].IsValid())
		{
			return FString::Printf(TEXT("%d 번 사실이 비었다"), Index);
		}

		for (int32 Earlier = 0; Earlier < Index; ++Earlier)
		{
			if (Facts[Earlier] == Facts[Index])
			{
				return FString::Printf(TEXT("사실 %s 가 두 번 있다"), *Facts[Index].ToString());
			}
		}
	}

	if (Value < 0 || Value > Facts.Num())
	{
		return FString::Printf(TEXT("기준값 %d 이 0 ~ %d 밖이다"), Value, Facts.Num());
	}
	// 늘 참인 조건은 오타다 - 깨우기, 존재 조건이 조용히 열린다.
	if ((Compare == EVBRuleCompare::AtLeast && Value == 0) || (Compare == EVBRuleCompare::AtMost && Value == Facts.Num()))
	{
		return FString::Printf(TEXT("늘 참이다(%s %d, 사실 %d개)"), Compare == EVBRuleCompare::AtLeast ? TEXT("AtLeast") : TEXT("AtMost"),
			Value, Facts.Num());
	}

	return FString();
}

// ---------------------------------------------------------------- 효과

void FVBEffect_SetFact::Apply(const FVBRuleContext& Context) const
{
	if (Context.Story)
	{
		Context.Story->SetFact(Fact, bOn);
	}
	else
	{
		VB_LOG(Warning, "효과(사실 %s): 문맥에 사실 저장소가 없다 - 적용 못 함", *Fact.ToString());
		Context.ReportEffectFailure();
	}
}

void FVBEffect_SetCounter::Apply(const FVBRuleContext& Context) const
{
	if (Context.Story)
	{
		Context.Story->SetCounter(Counter, Value);
	}
	else
	{
		VB_LOG(Warning, "효과(카운터 %s 정하기): 문맥에 사실 저장소가 없다 - 적용 못 함", *Counter.ToString());
	}
}

void FVBEffect_AddCounter::Apply(const FVBRuleContext& Context) const
{
	if (Context.Story)
	{
		Context.Story->AddToCounter(Counter, Delta);
	}
	else
	{
		VB_LOG(Warning, "효과(카운터 %s): 문맥에 사실 저장소가 없다 - 적용 못 함", *Counter.ToString());
	}
}

// ---------------------------------------------------------------- 배열

bool VBRules::EvaluateAll(const TArray<FInstancedStruct>& Conditions, const FVBRuleContext& Context)
{
	for (const FInstancedStruct& Entry : Conditions)
	{
		const FVBCondition* Condition = Entry.GetPtr<FVBCondition>();
		if (!Condition)
		{
			// 빈 칸이나 조건이 아닌 구조체. 참으로 처리하면 저작 실수가 "항상 열림" 으로 숨는다.
			VB_LOG(Warning, "조건 배열에 조건이 아닌 원소(%s) - 거짓으로 친다",
				Entry.GetScriptStruct() ? *Entry.GetScriptStruct()->GetName() : TEXT("빈 칸"));
			return false;
		}

		if (!Condition->Evaluate(Context))
		{
			return false;
		}
	}

	return true;
}

VBRules::EApplyResult VBRules::ApplyAllUntilFailure(const TArray<FInstancedStruct>& Effects,
	TFunctionRef<bool(FVBRuleContext&)> PrepareNext)
{
	for (int32 Index = 0; Index < Effects.Num(); ++Index)
	{
		FVBRuleContext Context;
		if (!PrepareNext(Context))
		{
			return EApplyResult::Interrupted;
		}
		// PrepareNext 가 부르는 쪽을 끝냈을 수 있다 - 원소는 그 뒤에 읽는다.
		if (Index >= Effects.Num())
		{
			return EApplyResult::Interrupted;
		}

		const FVBEffect* Effect = Effects[Index].GetPtr<FVBEffect>();
		if (!Effect)
		{
			VB_LOG(Warning, "효과 배열 %d번이 효과가 아니다 - 해내지 못한 것으로 멈춘다", Index);
			return EApplyResult::Failed;
		}

		const FString EffectName = Effects[Index].GetScriptStruct()->GetName();
		const int32 Remaining = Effects.Num() - Index - 1;
		bool bFailed = false;
		bool* const OuterSink = Context.FailureSink;
		Context.FailureSink = &bFailed;
		Effect->Apply(Context);
		if (bFailed)
		{
			if (OuterSink)
			{
				*OuterSink = true;
			}

			VB_LOG(Warning, "효과 %d번(%s)이 해내지 못했다 - 나머지 %d개를 적용하지 않는다", Index, *EffectName, Remaining);
			return EApplyResult::Failed;
		}
	}

	return EApplyResult::Landed;
}

bool VBRules::ApplyAll(const TArray<FInstancedStruct>& Effects, const FVBRuleContext& Context)
{
	if (!Context.CanApplyEffects())
	{
		VB_LOG(Warning, "효과 %d개를 클라에서 적용하려 했다 - 아무것도 안 바꾼다(효과는 서버만)", Effects.Num());
		return false;
	}

	for (const FInstancedStruct& Entry : Effects)
	{
		if (const FVBEffect* Effect = Entry.GetPtr<FVBEffect>())
		{
			Effect->Apply(Context);
		}
		else
		{
			VB_LOG(Warning, "효과 배열에 효과가 아닌 원소(%s) - 건너뛴다",
				Entry.GetScriptStruct() ? *Entry.GetScriptStruct()->GetName() : TEXT("빈 칸"));
		}
	}

	return true;
}

#if WITH_EDITOR
bool VBRules::ValidateStructArray(const TArray<FInstancedStruct>& Items, const UScriptStruct* Base, const FText& Where,
	FDataValidationContext& Context)
{
	bool bValid = true;
	for (int32 Index = 0; Index < Items.Num(); ++Index)
	{
		const UScriptStruct* Type = Items[Index].GetScriptStruct();
		if (!Type || !Type->IsChildOf(Base))
		{
			Context.AddError(FText::Format(NSLOCTEXT("VBRules", "BadStruct", "{0}: {1} 번 칸이 비었거나 {2} 가 아니다."),
				Where, Index, Base->GetDisplayNameText()));
			bValid = false;
		}
		else if (Type->IsChildOf(FVBCondition::StaticStruct()))
		{
			const FString Problem = Items[Index].Get<FVBCondition>().GetDataProblem();
			if (!Problem.IsEmpty())
			{
				Context.AddError(FText::Format(NSLOCTEXT("VBRules", "BadCondition", "{0}: {1} 번 조건 - {2}"),
					Where, Index, FText::FromString(Problem)));
				bValid = false;
			}
		}
	}

	return bValid;
}
#endif
