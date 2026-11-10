// ProjectVB Copyright (c) 2025. All Rights Reserved.

#include "PlayBot/VBPlayBotScenario.h"

#include "Misc/Paths.h"
#include "UObject/UnrealType.h"

namespace
{
	FString JsonEscape(const FString& In)
	{
		return In.ReplaceCharWithEscapedChar();
	}
}

bool FVBPlayBotVariant::Parse(const FString& Text, FString& OutError)
{
	TArray<FString> Parts;
	Text.ParseIntoArray(Parts, TEXT("|"), /*CullEmpty=*/false);
	Name = Parts.IsValidIndex(0) ? Parts[0].TrimStartAndEnd() : FString();
	if (Parts.Num() == 4)
	{
		ObjectPath = Parts[1].TrimStartAndEnd();
		Property = FName(*Parts[2].TrimStartAndEnd());
		Value = Parts[3].TrimStartAndEnd();
	}
	else if (Parts.Num() != 1)
	{
		OutError = FString::Printf(TEXT("Variant '%s' must be Name or Name|ObjectPath|Property|Value"), *Text);
		return false;
	}

	if (Name.IsEmpty())
	{
		OutError = FString::Printf(TEXT("Variant '%s' has no name"), *Text);
		return false;
	}

	return true;
}

bool FVBPlayBotRunConfig::Load(const FString& ScenarioName, FString& OutError)
{
	const FString Path = FPaths::Combine(FPaths::ProjectConfigDir(), TEXT("DefaultPlayBot.ini"));
	// 기호 처리를 켜고 합친다. 그냥 Read 하면 "+Key" 가 글자 그대로 키 이름이 되어 배열이 비어 읽힌다
	if (!Config.Combine(Path, /*bHandleSymbolCommands=*/true))
	{
		OutError = FString::Printf(TEXT("cannot read %s"), *Path);
		return false;
	}

	Name = ScenarioName;
	Section = FString::Printf(TEXT("Scenario.%s"), *ScenarioName);
	if (!Config.Contains(Section))
	{
		OutError = FString::Printf(TEXT("[%s] not found in %s"), *Section, *Path);
		return false;
	}

	FString Text;
	if (!RequireString(TEXT("Class"), Text, OutError) || !RequireString(TEXT("Map"), Map, OutError))
	{
		return false;
	}

	ClassName = FName(*Text);
	Seed = GetInt(TEXT("Seed"), 0);
	MaxRunSeconds = GetDouble(TEXT("MaxRunSeconds"), MaxRunSeconds);
	if (MaxRunSeconds <= 0.0)
	{
		OutError = FString::Printf(TEXT("[%s] MaxRunSeconds must be positive"), *Section);
		return false;
	}

	for (const FString& Value : GetArray(TEXT("TrackTags")))
	{
		const FGameplayTag Tag = FGameplayTag::RequestGameplayTag(FName(*Value), /*ErrorIfNotFound=*/false);
		if (!Tag.IsValid())
		{
			OutError = FString::Printf(TEXT("[%s] TrackTags %s is not a registered tag"), *Section, *Value);
			return false;
		}

		TrackTags.Add(Tag);
	}

	FString ProfileName;
	if (!RequireString(TEXT("Profile"), ProfileName, OutError))
	{
		return false;
	}

	return Profile.Load(Config, FString::Printf(TEXT("Profile.%s"), *ProfileName), OutError);
}

FString FVBPlayBotRunConfig::ToJson() const
{
	TArray<FString> Tags;
	for (const FGameplayTag& Tag : TrackTags)
	{
		Tags.Add(FString::Printf(TEXT("\"%s\""), *Tag.ToString()));
	}

	TArray<FString> Keys;
	if (const FConfigSection* Values = Config.FindSection(Section))
	{
		for (const TPair<FName, FConfigValue>& Pair : *Values)
		{
			Keys.Add(FString::Printf(TEXT("[\"%s\",\"%s\"]"), *Pair.Key.ToString(), *JsonEscape(Pair.Value.GetValue())));
		}
	}

	return FString::Printf(TEXT("{\"name\":\"%s\",\"class\":\"%s\",\"map\":\"%s\",\"seed\":%d,\"maxRunS\":%.1f,\"trackTags\":[%s],\"profile\":%s,\"keys\":[%s]}"),
		*JsonEscape(Name), *ClassName.ToString(), *JsonEscape(Map), Seed, MaxRunSeconds, *FString::Join(Tags, TEXT(",")),
		*Profile.ToJson(), *FString::Join(Keys, TEXT(",")));
}

FString FVBPlayBotRunConfig::GetString(const TCHAR* Key, const FString& Default) const
{
	FString Value;
	return Config.GetString(*Section, Key, Value) ? Value : Default;
}

double FVBPlayBotRunConfig::GetDouble(const TCHAR* Key, double Default) const
{
	double Value = Default;
	Config.GetDouble(*Section, Key, Value);
	return Value;
}

int32 FVBPlayBotRunConfig::GetInt(const TCHAR* Key, int32 Default) const
{
	int32 Value = Default;
	Config.GetInt(*Section, Key, Value);
	return Value;
}

TArray<FString> FVBPlayBotRunConfig::GetArray(const TCHAR* Key) const
{
	TArray<FString> Values;
	Config.GetArray(*Section, Key, Values);
	return Values;
}

bool FVBPlayBotRunConfig::RequireString(const TCHAR* Key, FString& Out, FString& OutError) const
{
	Out = GetString(Key);
	if (!Out.IsEmpty())
	{
		return true;
	}

	OutError = FString::Printf(TEXT("[%s] %s is required"), *Section, Key);
	return false;
}

bool FVBPlayBotOverrides::Apply(const FVBPlayBotVariant& Variant, FString& OutReadback, FString& OutError)
{
	OutReadback.Reset();
	OutError.Reset();
	if (Variant.IsAuthored())
	{
		RestoreAll();
		OutReadback = TEXT("authored");
		return true;
	}

	UObject* Object = LoadObject<UObject>(nullptr, *Variant.ObjectPath);
	FProperty* Property = Object ? Object->GetClass()->FindPropertyByName(Variant.Property) : nullptr;
	if (!Property)
	{
		OutError = FString::Printf(TEXT("variant %s: %s.%s not found"), *Variant.Name, *Variant.ObjectPath, *Variant.Property.ToString());
		return false;
	}

	const bool bKnown = Originals.ContainsByPredicate([Object, &Variant](const FOriginal& Original)
	{
		return Original.Object.Get() == Object && Original.Property == Variant.Property;
	});
	if (!bKnown)
	{
		FOriginal& Original = Originals.AddDefaulted_GetRef();
		Original.Object = Object;
		Original.Path = Variant.ObjectPath;
		Original.Property = Variant.Property;
		Property->ExportText_InContainer(0, Original.Text, Object, Object, Object, PPF_None);
	}

	if (!Property->ImportText_InContainer(*Variant.Value, Object, Object, PPF_None, nullptr))
	{
		OutError = FString::Printf(TEXT("variant %s: value '%s' does not parse for %s"), *Variant.Name, *Variant.Value,
			*Variant.Property.ToString());
		return false;
	}

	Property->ExportText_InContainer(0, OutReadback, Object, Object, Object, PPF_None);
	return true;
}

void FVBPlayBotOverrides::RestoreAll()
{
	for (const FOriginal& Original : Originals)
	{
		const FString Name = FString::Printf(TEXT("%s.%s"), *Original.Path, *Original.Property.ToString());
		UObject* Object = Original.Object.Get();
		if (!Object)
		{
			Report.Unavailable.Add(Name);
			continue;
		}

		FProperty* Property = Object->GetClass()->FindPropertyByName(Original.Property);
		if (!Property || !Property->ImportText_InContainer(*Original.Text, Object, Object, PPF_None, nullptr))
		{
			Report.Failures.Add(FString::Printf(TEXT("%s: cannot write back '%s'"), *Name, *Original.Text));
			continue;
		}
		// 다시 읽어 본다. 파싱은 됐어도 다른 값이 들어갔으면 되돌린 것이 아니다
		FString Readback;
		Property->ExportText_InContainer(0, Readback, Object, Object, Object, PPF_None);
		if (Readback != Original.Text)
		{
			Report.Failures.Add(FString::Printf(TEXT("%s: wrote '%s', reads '%s'"), *Name, *Original.Text, *Readback));
			continue;
		}

		++Report.Restored;
	}

	Originals.Reset();
}

FString FVBPlayBotRestoreReport::ToJson() const
{
	const auto List = [](const TArray<FString>& Values)
	{
		TArray<FString> Quoted;
		for (const FString& Value : Values)
		{
			Quoted.Add(FString::Printf(TEXT("\"%s\""), *JsonEscape(Value)));
		}

		return FString::Join(Quoted, TEXT(","));
	};
	return FString::Printf(TEXT("{\"restored\":%d,\"unavailable\":[%s],\"failures\":[%s]}"), Restored, *List(Unavailable), *List(Failures));
}
