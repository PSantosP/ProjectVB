// ProjectVB Copyright (c) 2025. All Rights Reserved.

#include "PlayBot/VBPlayBotRecorder.h"

#include "AudioDevice.h"
#include "AudioMixerDevice.h"
#include "AudioThread.h"
#include "HAL/IConsoleManager.h"
#include "AudioMixerPlatformNonRealtime.h"
#include "Engine/Engine.h"
#include "Engine/GameViewportClient.h"
#include "Engine/World.h"
#include "HAL/FileManager.h"
#include "Misc/App.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "PlayBot/VBPlayBotHumanModel.h"
#include "UnrealClient.h"
#include "ImageUtils.h"
#include "Async/Async.h"
#include "Modules/ModuleManager.h"
#include "HAL/PlatformTime.h"

namespace VBPlayBotRecorderAudio
{
	// 녹음 버퍼를 미리 잡아 두는 길이(초). 더 긴 실행도 녹음되고 버퍼가 늘어날 뿐이다.
	constexpr float ExpectedRecordingSeconds = 900.0f;

	FAudioDevice* GameAudioDevice()
	{
		const UWorld* World = GEngine && GEngine->GameViewport ? GEngine->GameViewport->GetWorld() : nullptr;
		return World ? World->GetAudioDeviceRaw() : nullptr;
	}

	// 엔진의 non-realtime 플랫폼은 프로세스를 -DeterministicAudio 로 띄웠을 때만 있다.
	Audio::FMixerPlatformNonRealtime* NonRealtimePlatform(FAudioDevice* Device)
	{
		if (!Device || !Device->IsNonRealtime())
		{
			return nullptr;
		}

		Audio::IAudioMixerPlatformInterface* Platform = static_cast<Audio::FMixerDevice*>(Device)->GetAudioMixerPlatform();
		return Platform && Platform->IsNonRealtime() ? static_cast<Audio::FMixerPlatformNonRealtime*>(Platform) : nullptr;
	}

	// 섞인 float 샘플을 16-bit PCM WAV 로 돌아오기 전에 쓴다(실행 직후 프로세스가 끝난다).
	bool WriteWav(const FString& Path, const Audio::FAlignedFloatBuffer& Samples, int32 Channels, int32 SampleRate)
	{
		TArray<uint8> Bytes;
		const uint32 DataBytes = static_cast<uint32>(Samples.Num()) * sizeof(int16);
		auto Put32 = [&Bytes](uint32 V) { for (int32 I = 0; I < 4; ++I) Bytes.Add(static_cast<uint8>((V >> (8 * I)) & 0xFF)); };
		auto Put16 = [&Bytes](uint16 V) { Bytes.Add(static_cast<uint8>(V & 0xFF)); Bytes.Add(static_cast<uint8>(V >> 8)); };
		auto PutTag = [&Bytes](const char* Tag) { for (int32 I = 0; I < 4; ++I) Bytes.Add(static_cast<uint8>(Tag[I])); };
		Bytes.Reserve(44 + DataBytes);
		PutTag("RIFF"); Put32(36 + DataBytes); PutTag("WAVE");
		PutTag("fmt "); Put32(16); Put16(1); Put16(static_cast<uint16>(Channels)); Put32(SampleRate);
		Put32(SampleRate * Channels * sizeof(int16)); Put16(static_cast<uint16>(Channels * sizeof(int16))); Put16(16);
		PutTag("data"); Put32(DataBytes);
		for (const float Sample : Samples)
		{
			Put16(static_cast<uint16>(static_cast<int16>(FMath::RoundToInt(FMath::Clamp(Sample, -1.0f, 1.0f) * 32767.0f))));
		}

		return FFileHelper::SaveArrayToFile(Bytes, *Path);
	}
}

FVBPlayBotRecorder::~FVBPlayBotRecorder()
{
	Close();
}

bool FVBPlayBotRecorder::Open(const FString& OutDir, const FString& InFFmpegPath, FString& OutError)
{
	Dir = OutDir;
	FFmpegPath = InFFmpegPath;
	if (!IFileManager::Get().MakeDirectory(*Dir, true))
	{
		OutError = FString::Printf(TEXT("cannot create output dir %s"), *Dir);
		return false;
	}

	if (!bNative && !FPaths::FileExists(FFmpegPath))
	{
		OutError = FString::Printf(TEXT("ffmpeg not found at %s"), *FFmpegPath);
		return false;
	}

	if (bNative && FPaths::FileExists(FPaths::Combine(Dir, TEXT("telemetry.jsonl"))))
	{
		OutError = TEXT("native output already contains telemetry; use a fresh directory");
		return false;
	}
	Telemetry.Reset(IFileManager::Get().CreateFileWriter(*FPaths::Combine(Dir, TEXT("telemetry.jsonl"))));
	if (!Telemetry)
	{
		OutError = TEXT("cannot open telemetry.jsonl");
		return false;
	}
	// -DeterministicAudio 면 엔진도 audio thread 에서 tick 마다 mixer 를 렌더해 두 렌더러가 경쟁한다(Quartz assert).
	// Movie Render Queue 처럼 넘겨받는다: 엔진 렌더를 끄고 audio thread 가 비기를 기다린 뒤 프레임마다 한 게임 step 을 렌더한다.
	// 넘겨받기는 non-realtime mixer 를 처음 찾은 프레임(PumpAudio)에 일어난다.
	if (!bNative)
	{
		AudioTicker = FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateRaw(this, &FVBPlayBotRecorder::PumpAudio));
	}
	return true;
}

bool FVBPlayBotRecorder::PumpAudio(float /*RealDeltaSeconds*/)
{
	if (Audio::FMixerPlatformNonRealtime* Platform = VBPlayBotRecorderAudio::NonRealtimePlatform(VBPlayBotRecorderAudio::GameAudioDevice()))
	{
		if (!bAudioTakenOver)
		{
			if (IConsoleVariable* RenderEveryTick = IConsoleManager::Get().FindConsoleVariable(TEXT("au.nrt.RenderEveryTick")))
			{
				PreviousRenderEveryTick = RenderEveryTick->GetInt();
				RenderEveryTick->SetWithCurrentPriority(0);
			}
			// 조용한 submix 는 스스로 꺼져 녹음에 붙기 전에 돌아가므로 조용한 구간이 빠지고 소리가 밀린다.
			// 엔진의 "무음도 렌더해야 함" 스위치로 계속 렌더하게 한다.
			if (IConsoleVariable* NeverDisable = IConsoleManager::Get().FindConsoleVariable(TEXT("au.NeverDisableSubmixes")))
			{
				PreviousNeverDisableSubmixes = NeverDisable->GetInt();
				NeverDisable->SetWithCurrentPriority(1);
			}

			FAudioCommandFence Fence;
			Fence.BeginFence();
			Fence.Wait();
			bAudioTakenOver = true;
			WriteLine(FString::Printf(TEXT("{\"k\":\"ev\",\"e\":\"audio\",\"state\":\"rendering\",\"f\":%llu,\"step_s\":%.6f}"), GFrameCounter, FApp::GetDeltaTime()));
		}
		// play bot 에서 엔진 프레임 delta 는 고정 step 이라 영상 프레임과 같은 시계다.
		// game thread 에서 렌더하면 출력 큐가 device lock 경쟁에 져 블록이 빠지므로, audio thread 에서 렌더하고 기다린다.
		const double Step = FApp::GetDeltaTime();
		FAudioThread::RunCommandOnAudioThread([Platform, Step]() { Platform->RenderAudio(Step); });
		FAudioCommandFence Fence;
		Fence.BeginFence();
		Fence.Wait();
		if (bRecordingAudio)
		{
			AudioRenderedSeconds += Step;
		}
	}

	return true;
}

void FVBPlayBotRecorder::StartAudio()
{
	FAudioDevice* Device = VBPlayBotRecorderAudio::GameAudioDevice();
	if (!VBPlayBotRecorderAudio::NonRealtimePlatform(Device))
	{
		// realtime mixer 는 벽시계로 스피커에 재생하므로 녹음해도 게임 시간 영상과 맞지 않는다.
		WriteLine(TEXT("{\"k\":\"ev\",\"e\":\"audio\",\"state\":\"off\",\"why\":\"no non-realtime mixer (-DeterministicAudio missing) - the video has no audio\"}"));
		return;
	}
	// 스크린샷이 영상 프레임 0 이 되는 프레임에 불린다. 다음 pump 가 그 프레임의 오디오부터 렌더한다.
	Device->StartRecording(nullptr, VBPlayBotRecorderAudio::ExpectedRecordingSeconds);
	bRecordingAudio = true;
	AudioRenderedSeconds = 0.0;
	WriteLine(FString::Printf(TEXT("{\"k\":\"ev\",\"e\":\"audio\",\"state\":\"recording\",\"f\":%llu}"), GFrameCounter));
}

void FVBPlayBotRecorder::FinishAudio()
{
	if (!bRecordingAudio)
	{
		return;
	}

	bRecordingAudio = false;
	FAudioDevice* Device = VBPlayBotRecorderAudio::GameAudioDevice();
	if (!Device)
	{
		WriteLine(TEXT("{\"k\":\"ev\",\"e\":\"audio\",\"state\":\"lost\",\"why\":\"no audio device at the end of the run\"}"));
		return;
	}

	float Channels = 0.0f;
	float SampleRate = 0.0f;
	const Audio::FAlignedFloatBuffer& Samples = Device->StopRecording(nullptr, Channels, SampleRate);
	const FString Path = FPaths::Combine(Dir, TEXT("audio.wav"));
	const bool bWritten = Samples.Num() > 0 && Channels > 0.0f && SampleRate > 0.0f
		&& VBPlayBotRecorderAudio::WriteWav(Path, Samples, FMath::RoundToInt(Channels), FMath::RoundToInt(SampleRate));
	WriteLine(FString::Printf(TEXT("{\"k\":\"ev\",\"e\":\"audio\",\"state\":\"%s\",\"samples\":%d,\"channels\":%.0f,\"rate\":%.0f,\"rendered_s\":%.4f}"),
		bWritten ? TEXT("written") : TEXT("lost"), Samples.Num(), Channels, SampleRate, AudioRenderedSeconds));
}

void FVBPlayBotRecorder::WriteLine(const FString& Json)
{
	if (!Telemetry)
	{
		return;
	}
	// FString::ReplaceCharWithEscapedChar 는 JSON 이 거부하는 C escape(\' \? \a \v)를 낸다. 한 줄만 있어도 json.loads 가
	// 파일 전체를 못 읽으므로 모든 작성자의 줄을 여기서 고친다.
	FString Line;
	Line.Reserve(Json.Len());
	for (int32 Index = 0; Index < Json.Len(); ++Index)
	{
		const TCHAR C = Json[Index];
		if (C != TEXT('\\') || Index + 1 >= Json.Len())
		{
			Line.AppendChar(C);
			continue;
		}

		const TCHAR Next = Json[++Index];
		switch (Next)
		{
		case TEXT('"'): case TEXT('\\'): case TEXT('/'): case TEXT('b'): case TEXT('f'): case TEXT('n'): case TEXT('r'): case TEXT('t'): case TEXT('u'):
			Line.AppendChar(C);
			Line.AppendChar(Next);
			break;
		case TEXT('a'): Line += TEXT("\\u0007"); break;
		case TEXT('v'): Line += TEXT("\\u000b"); break;
		default: Line.AppendChar(Next); break; // \' and \? become the plain character
		}
	}

	const FTCHARToUTF8 Utf8(*Line);
	Telemetry->Serialize(const_cast<ANSICHAR*>(Utf8.Get()), Utf8.Length());
	static const ANSICHAR NewLine = '\n';
	Telemetry->Serialize(const_cast<ANSICHAR*>(&NewLine), 1);
}

void FVBPlayBotRecorder::StartCapture()
{
	if (bCapturing)
	{
		return;
	}

	ScreenshotHandle = UGameViewportClient::OnScreenshotCaptured().AddRaw(this, &FVBPlayBotRecorder::HandleScreenshot);
	bCapturing = true;
	if (!bNative) { StartAudio(); }
}

void FVBPlayBotRecorder::RequestFrame()
{
	if (!bNative && bCapturing && !HasFailed())
	{
		FScreenshotRequest::RequestScreenshot( /*bInShowUI=*/true);
	}
}

void FVBPlayBotRecorder::StopCapture()
{
	if (!bCapturing)
	{
		return;
	}

	UGameViewportClient::OnScreenshotCaptured().Remove(ScreenshotHandle);
	ScreenshotHandle.Reset();
	bCapturing = false;
}


bool FVBPlayBotRecorder::FinalizeNativeCapture()
{
	if (!bNative) { return !HasFailed(); }
	StopCapture();
	if (bNativePending)
	{
		bNativePending = false;
		FScreenshotRequest::Reset();
		if (!HasFailed()) { Failure = TEXT("native capture ended before its requested screenshot arrived"); }
	}
	PumpNativeFrames(true);
	return !HasFailed() && !HasPendingNativeFrame();
}

void FVBPlayBotRecorder::Close()
{
	FinalizeNativeCapture();
	FinishAudio();
	if (AudioTicker.IsValid())
	{
		FTSTicker::RemoveTicker(AudioTicker);
		AudioTicker.Reset();
	}

	if (bAudioTakenOver)
	{
		// 남은 프로세스 동안은 렌더를 엔진에 돌려준다(종료 중에도 mix 한다).
		if (IConsoleVariable* RenderEveryTick = IConsoleManager::Get().FindConsoleVariable(TEXT("au.nrt.RenderEveryTick")))
		{
			RenderEveryTick->SetWithCurrentPriority(PreviousRenderEveryTick);
		}

		if (IConsoleVariable* NeverDisable = IConsoleManager::Get().FindConsoleVariable(TEXT("au.NeverDisableSubmixes")))
		{
			NeverDisable->SetWithCurrentPriority(PreviousNeverDisableSubmixes);
		}

		bAudioTakenOver = false;
	}

	StopCapture();
	if (PipeWrite)
	{
		// stdin 을 닫으면 encoder 입력이 끝난다. 프로세스가 끝나기 전에 mp4 가 마무리되도록 기다린다.
		FPlatformProcess::ClosePipe(PipeRead, PipeWrite);
		PipeRead = nullptr;
		PipeWrite = nullptr;
	}

	if (Encoder.IsValid())
	{
		FPlatformProcess::WaitForProc(Encoder);
		int32 Code = 0;
		FPlatformProcess::GetProcReturnCode(Encoder, &Code);
		FPlatformProcess::CloseProc(Encoder);
		WriteLine(FString::Printf(TEXT("{\"k\":\"ev\",\"e\":\"encoder_exit\",\"code\":%d,\"frames\":%lld}"), Code, VideoFrame));
		if (Code != 0 && Failure.IsEmpty())
		{
			Failure = FString::Printf(TEXT("ffmpeg exited with %d"), Code);
		}
	}

	if (Telemetry)
	{
		Telemetry->Close();
		Telemetry.Reset();
	}
}

void FVBPlayBotRecorder::StampMarker(TArray<FColor>& Pixels, int32 Width, int32 Height, uint32 Value)
{
	const int32 Blocks = MarkerBits + 2;
	if (Width < Blocks * MarkerBlock || Height < MarkerBlock || Pixels.Num() < Width * Height)
	{
		return;
	}

	for (int32 Block = 0; Block < Blocks; ++Block)
	{
		const bool bGuard = Block == 0 || Block == Blocks - 1;
		const bool bOn = bGuard || ((Value >> (Block - 1)) & 1u) != 0;
		const FColor Color = bOn ? FColor::White : FColor::Black;
		for (int32 Y = 0; Y < MarkerBlock; ++Y)
		{
			FColor* Row = Pixels.GetData() + Y * Width + Block * MarkerBlock;
			for (int32 X = 0; X < MarkerBlock; ++X)
			{
				Row[X] = Color;
			}
		}
	}
}

void FVBPlayBotRecorder::HandleScreenshot(int32 Width, int32 Height, const TArray<FColor>& Colors)
{
	if (!bCapturing || HasFailed())
	{
		return;
	}

	if (bNative)
	{
		WriteNativeFrame(Width, Height, Colors);
		return;
	}
	if (FrameSize == FIntPoint::ZeroValue && !StartEncoder(Width, Height))
	{
		return;
	}

	if (FrameSize != FIntPoint(Width, Height))
	{
		Failure = FString::Printf(TEXT("viewport size changed during capture (%dx%d -> %dx%d)"), FrameSize.X, FrameSize.Y, Width, Height);
		return;
	}
	// 이 프레임의 Draw 안에서 방송되므로 GFrameCounter 가 이 프레임 telemetry 샘플의 id 다.
	const uint64 EngineFrame = GFrameCounter;
	Scratch = Colors;
	StampMarker(Scratch, Width, Height, static_cast<uint32>(EngineFrame & 0xFFFFFFFFull));
	if (!WritePipeAll(reinterpret_cast<const uint8*>(Scratch.GetData()), Scratch.Num() * sizeof(FColor)))
	{
		Failure = TEXT("writing a frame to ffmpeg failed");
		return;
	}

	WriteLine(FString::Printf(TEXT("{\"k\":\"cap\",\"vf\":%lld,\"f\":%llu}"), VideoFrame, EngineFrame));
	++VideoFrame;
}

bool FVBPlayBotRecorder::StartEncoder(int32 Width, int32 Height)
{
	if (!FPlatformProcess::CreatePipe(PipeRead, PipeWrite, /*bWritePipeLocal=*/true))
	{
		Failure = TEXT("cannot create the encoder pipe");
		return false;
	}

	const FString Output = FPaths::ConvertRelativePathToFull(FPaths::Combine(Dir, TEXT("video.mp4")));
	// 게임 step 하나에 영상 프레임 하나 - encoder rate 는 엔진 고정 step(playbot.sh -FPS)이 유일한 주인이다.
	// 고정 step 이 없으면 60 은 이름표일 뿐이라 encoder_start 가 fixed=false 를 남겨 playbot_clock.py 가 UNKNOWN 으로 본다.
	const bool bFixedStep = FApp::UseFixedTimeStep() && FApp::GetFixedDeltaTime() > 0.0;
	const double Fps = bFixedStep ? 1.0 / FApp::GetFixedDeltaTime() : 60.0;
	// 원본 BGRA 를 넣고 보낸 프레임을 그대로 둔다(passthrough). 거의 무손실이라 디코딩 뒤에도 마커가 남는다.
	const FString Args = FString::Printf(
		TEXT("-hide_banner -loglevel error -y -f rawvideo -pix_fmt bgra -s %dx%d -framerate %.6f -i - ")
		TEXT("-fps_mode passthrough -c:v libx264 -preset veryfast -crf 12 -pix_fmt yuv420p \"%s\""),
		Width, Height, Fps, *Output);
	Encoder = FPlatformProcess::CreateProc(*FFmpegPath, *Args, false, true, true, nullptr, 0, nullptr, nullptr, PipeRead);
	if (!Encoder.IsValid())
	{
		Failure = TEXT("cannot start ffmpeg");
		return false;
	}

	FrameSize = FIntPoint(Width, Height);
	WriteLine(FString::Printf(TEXT("{\"k\":\"ev\",\"e\":\"encoder_start\",\"w\":%d,\"h\":%d,\"fps\":%.6f,\"fixed\":%s}"), Width, Height, Fps,
		bFixedStep ? TEXT("true") : TEXT("false")));
	return true;
}

bool FVBPlayBotRecorder::WritePipeAll(const uint8* Data, int32 Size)
{
	int32 Offset = 0;
	while (Offset < Size)
	{
		int32 Written = 0;
		if (!FPlatformProcess::WritePipe(PipeWrite, Data + Offset, Size - Offset, &Written) || Written <= 0)
		{
			return false;
		}

		Offset += Written;
	}

	return true;
}


bool FVBPlayBotRecorder::OpenNative(const FString& OutDir, const FVBPlayBotNativeCapture& Limits, FString& OutError)
{
	if (!FMath::IsFinite(Limits.StartSeconds) || Limits.StartSeconds <= 0.0
		|| !FMath::IsFinite(Limits.IntervalSeconds) || Limits.IntervalSeconds <= 0.0
		|| !FMath::IsFinite(Limits.MaxGapSeconds) || Limits.MaxGapSeconds < Limits.IntervalSeconds
		|| Limits.MaxBytes <= 0 || Limits.MaxFrames <= 0)
	{
		OutError = TEXT("native capture requires finite clock/interval/gap and positive storage limits");
		return false;
	}
	if (!FModuleManager::Get().LoadModule(TEXT("ImageWrapper")))
	{
		OutError = TEXT("native PNG codec module is unavailable");
		return false;
	}
	bNative = true;
	Native = Limits;
	if (!Open(OutDir, FString(), OutError)) { return false; }
	if (!IFileManager::Get().MakeDirectory(*FPaths::Combine(Dir, TEXT("native")), true))
	{
		OutError = TEXT("cannot create native frame directory");
		return false;
	}
	WriteLine(TEXT("{\"k\":\"native_capture\",\"audio\":\"Unavailable\",\"queue_limit\":2,\"video\":\"PendingOfflinePTSProof\"}"));
	return true;
}

void FVBPlayBotRecorder::RequestNativeFrame(int32 WorldEpoch, bool bAllowNewRequest)
{
	if (!bNative || !bCapturing || HasFailed()) { return; }
	PumpNativeFrames(false);
	if (HasFailed()) { return; }
	const double Now = FPlatformTime::Seconds() - Native.StartSeconds;
	if (bNativePending)
	{
		if (Now - NativeRequestSeconds > Native.MaxGapSeconds) { Failure = TEXT("native screenshot request timed out"); }
		return;
	}
	if (!bAllowNewRequest) { return; }
	if (LastNativeCaptureSeconds >= 0.0 && Now - LastNativeCaptureSeconds > Native.MaxGapSeconds)
	{
		Failure = TEXT("native capture gap exceeded the declared limit");
		return;
	}
	if (LastNativeCaptureSeconds >= 0.0 && Now - LastNativeCaptureSeconds < Native.IntervalSeconds) { return; }
	if (NativeJobs.Num() >= 2)
	{
		Failure = TEXT("native capture worker queue is full");
		return;
	}
	if (FScreenshotRequest::IsScreenshotRequested())
	{
		Failure = TEXT("another screenshot request overlaps native capture");
		return;
	}
	NativeEpoch = WorldEpoch;
	NativeRequestSeconds = Now;
	bNativePending = true;
	FScreenshotRequest::RequestScreenshot(true);
}

void FVBPlayBotRecorder::WriteNativeFrame(int32 Width, int32 Height, const TArray<FColor>& Colors)
{
	const double Started = FPlatformTime::Seconds();
	const double Now = Started - Native.StartSeconds;
	if (!bNativePending || Now <= LastNativeCaptureSeconds || Width <= 0 || Height <= 0
		|| Width > 1920 || Height > 1080 || Colors.Num() != Width * Height || NativeRequestedFrames >= Native.MaxFrames
		|| NativeJobs.Num() >= 2)
	{
		Failure = TEXT("invalid, unexpected or over-budget native screenshot");
		return;
	}
	if (FrameSize != FIntPoint::ZeroValue && FrameSize != FIntPoint(Width, Height))
	{
		Failure = TEXT("native viewport changed; a new capture segment is required");
		return;
	}
	const int64 Reservation = static_cast<int64>(Width) * Height * 4 + 65536;
	if (NativeBytes + NativeReservedBytes + Reservation > Native.MaxBytes)
	{
		Failure = TEXT("native capture byte budget cannot reserve another frame");
		return;
	}
	FrameSize = FIntPoint(Width, Height);
	FNativeFrameJob Job;
	Job.Index = NativeRequestedFrames++;
	Job.EngineFrame = GFrameCounter;
	Job.Epoch = NativeEpoch;
	Job.Seconds = Now;
	Job.ReservedBytes = Reservation;
	Job.Relative = FString::Printf(TEXT("native/frame%06lld.png"), Job.Index);
	TArray<FColor> Pixels = Colors;
	StampMarker(Pixels, Width, Height, static_cast<uint32>(Job.EngineFrame & 0xFFFFFFFFull));
	const FString Path = FPaths::Combine(Dir, Job.Relative);
	Job.Future = Async(EAsyncExecution::ThreadPool, [Pixels = MoveTemp(Pixels), Width, Height, Path, Reservation]()
	{
		FNativeFrameResult Result;
		const double WorkStart = FPlatformTime::Seconds();
		TArray64<uint8> Png;
		FImageUtils::PNGCompressImageArray(Width, Height, TArrayView64<const FColor>(Pixels.GetData(), Pixels.Num()), Png);
		if (Png.IsEmpty() || Png.Num() > Reservation)
		{
			Result.Error = TEXT("native compression failed or exceeded its reserved byte budget");
		}
		else if (!FFileHelper::SaveArrayToFile(Png, *Path))
		{
			Result.Error = TEXT("native worker PNG write failed");
		}
		else { Result.Bytes = Png.Num(); }
		Result.WorkMs = (FPlatformTime::Seconds() - WorkStart) * 1000.0;
		return Result;
	});
	Job.MainThreadMs = (FPlatformTime::Seconds() - Started) * 1000.0;
	NativeReservedBytes += Reservation;
	NativeJobs.Add(MoveTemp(Job));
	LastNativeCaptureSeconds = Now;
	bNativePending = false;
}

void FVBPlayBotRecorder::PumpNativeFrames(bool bDrain)
{
	while (!NativeJobs.IsEmpty())
	{
		FNativeFrameJob& Job = NativeJobs[0];
		if (!Job.Future.IsReady() && (!bDrain || !Job.Future.WaitFor(FTimespan::FromSeconds(5.0))))
		{
			if (bDrain || FPlatformTime::Seconds() - Native.StartSeconds - Job.Seconds > 5.0)
			{ Failure = TEXT("native worker did not finish within five seconds"); }
			return;
		}
		const FNativeFrameResult Result = Job.Future.Get();
		NativeReservedBytes -= Job.ReservedBytes;
		if (!Result.Error.IsEmpty()) { Failure = Result.Error; }
		else
		{
			NativeBytes += Result.Bytes;
			WriteLine(FString::Printf(TEXT("{\"k\":\"native_cap\",\"vf\":%lld,\"f\":%llu,\"rt\":%.9f,\"epoch\":%d,\"image\":\"%s\",\"main_thread_ms\":%.3f,\"worker_ms\":%.3f,\"bytes\":%lld}"),
				Job.Index, Job.EngineFrame, Job.Seconds, Job.Epoch, *Job.Relative, Job.MainThreadMs, Result.WorkMs, Result.Bytes));
			++VideoFrame;
		}
		NativeJobs.RemoveAt(0);
	}
	Flush();
}
