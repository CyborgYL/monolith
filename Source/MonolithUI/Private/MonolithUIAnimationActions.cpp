// MonolithUIAnimationActions.cpp
#include "MonolithUIAnimationActions.h"
#include "MonolithUIInternal.h"
#include "MonolithParamSchema.h"
#include "Animation/WidgetAnimation.h"
#include "MovieScene.h"
#include "Tracks/MovieSceneFloatTrack.h"
#include "Channels/MovieSceneFloatChannel.h"
#include "Tracks/MovieScene3DTransformTrack.h"
#include "Sections/MovieSceneFloatSection.h"
#include "Sections/MovieScene3DTransformSection.h"
#include "Channels/MovieSceneChannelProxy.h"
#include "Kismet2/KismetEditorUtilities.h"

void FMonolithUIAnimationActions::RegisterActions(FMonolithToolRegistry& Registry)
{
    Registry.RegisterAction(
        TEXT("ui"), TEXT("list_animations"),
        TEXT("List all UWidgetAnimation assets on a Widget Blueprint"),
        FMonolithActionHandler::CreateStatic(&HandleListAnimations),
        FParamSchemaBuilder()
            .Required(TEXT("asset_path"), TEXT("string"), TEXT("Widget Blueprint asset path"))
            .Build()
    );

    Registry.RegisterAction(
        TEXT("ui"), TEXT("get_animation_details"),
        TEXT("Get tracks and keyframes for a specific animation on a Widget Blueprint"),
        FMonolithActionHandler::CreateStatic(&HandleGetAnimationDetails),
        FParamSchemaBuilder()
            .Required(TEXT("asset_path"), TEXT("string"), TEXT("Widget Blueprint asset path"))
            .Required(TEXT("animation_name"), TEXT("string"), TEXT("Name of the UWidgetAnimation"))
            .Build()
    );

    Registry.RegisterAction(
        TEXT("ui"), TEXT("create_animation"),
        TEXT("Create a new UWidgetAnimation with tracks and keyframes on a Widget Blueprint"),
        FMonolithActionHandler::CreateStatic(&HandleCreateAnimation),
        FParamSchemaBuilder()
            .Required(TEXT("asset_path"), TEXT("string"), TEXT("Widget Blueprint asset path"))
            .Required(TEXT("animation_name"), TEXT("string"), TEXT("Name for the new animation"))
            .Required(TEXT("duration"), TEXT("number"), TEXT("Animation duration in seconds"))
            .Optional(TEXT("tracks"), TEXT("array"), TEXT("Array of track definitions: [{\"widget_name\": \"MyWidget\", \"property\": \"opacity|transform|color\", \"keyframes\": [{\"time\": 0.0, \"value\": 1.0}]}]"))
            .Build()
    );

    Registry.RegisterAction(
        TEXT("ui"), TEXT("add_animation_keyframe"),
        TEXT("Add a keyframe to an existing animation track"),
        FMonolithActionHandler::CreateStatic(&HandleAddAnimationKeyframe),
        FParamSchemaBuilder()
            .Required(TEXT("asset_path"), TEXT("string"), TEXT("Widget Blueprint asset path"))
            .Required(TEXT("animation_name"), TEXT("string"), TEXT("Name of the UWidgetAnimation"))
            .Required(TEXT("widget_name"), TEXT("string"), TEXT("Target widget name"))
            .Required(TEXT("property"), TEXT("string"), TEXT("Property: opacity, transform, color"))
            .Required(TEXT("time"), TEXT("number"), TEXT("Keyframe time in seconds"))
            .Required(TEXT("value"), TEXT("number"), TEXT("Keyframe value (float for opacity, etc.)"))
            .Build()
    );

    Registry.RegisterAction(
        TEXT("ui"), TEXT("remove_animation"),
        TEXT("Remove a UWidgetAnimation from a Widget Blueprint"),
        FMonolithActionHandler::CreateStatic(&HandleRemoveAnimation),
        FParamSchemaBuilder()
            .Required(TEXT("asset_path"), TEXT("string"), TEXT("Widget Blueprint asset path"))
            .Required(TEXT("animation_name"), TEXT("string"), TEXT("Name of the animation to remove"))
            .Build()
    );
}

// --- list_animations ---
FMonolithActionResult FMonolithUIAnimationActions::HandleListAnimations(const TSharedPtr<FJsonObject>& Params)
{
    FString AssetPath = Params->GetStringField(TEXT("asset_path"));
    FMonolithActionResult Err;
    UWidgetBlueprint* WBP = MonolithUIInternal::LoadWidgetBlueprint(AssetPath, Err);
    if (!WBP) return Err;

    TArray<TSharedPtr<FJsonValue>> AnimArray;

    for (UWidgetAnimation* Anim : WBP->Animations)
    {
        if (!Anim) continue;

        TSharedPtr<FJsonObject> AnimObj = MakeShared<FJsonObject>();
        AnimObj->SetStringField(TEXT("name"), Anim->GetName());

        UMovieScene* MovieScene = Anim->GetMovieScene();
        if (MovieScene)
        {
            FFrameRate TickRes = MovieScene->GetTickResolution();
            TRange<FFrameNumber> PlayRange = MovieScene->GetPlaybackRange();

            double StartTime = PlayRange.GetLowerBoundValue().Value / TickRes.AsDecimal();
            double EndTime = PlayRange.GetUpperBoundValue().Value / TickRes.AsDecimal();

            AnimObj->SetNumberField(TEXT("start_time"), StartTime);
            AnimObj->SetNumberField(TEXT("end_time"), EndTime);
            AnimObj->SetNumberField(TEXT("binding_count"), MovieScene->GetBindings().Num());
        }

        AnimArray.Add(MakeShared<FJsonValueObject>(AnimObj));
    }

    TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
    Result->SetStringField(TEXT("asset_path"), AssetPath);
    Result->SetArrayField(TEXT("animations"), AnimArray);
    Result->SetNumberField(TEXT("count"), AnimArray.Num());
    return FMonolithActionResult::Success(Result);
}

// --- get_animation_details ---
FMonolithActionResult FMonolithUIAnimationActions::HandleGetAnimationDetails(const TSharedPtr<FJsonObject>& Params)
{
    FString AssetPath = Params->GetStringField(TEXT("asset_path"));
    FString AnimationName = Params->GetStringField(TEXT("animation_name"));

    FMonolithActionResult Err;
    UWidgetBlueprint* WBP = MonolithUIInternal::LoadWidgetBlueprint(AssetPath, Err);
    if (!WBP) return Err;

    // Find the animation by name
    UWidgetAnimation* TargetAnim = nullptr;
    for (UWidgetAnimation* Anim : WBP->Animations)
    {
        if (Anim && Anim->GetName() == AnimationName)
        {
            TargetAnim = Anim;
            break;
        }
    }

    if (!TargetAnim)
    {
        return FMonolithActionResult::Error(
            FString::Printf(TEXT("Animation '%s' not found on '%s'"), *AnimationName, *AssetPath));
    }

    UMovieScene* MovieScene = TargetAnim->GetMovieScene();
    if (!MovieScene)
    {
        return FMonolithActionResult::Error(TEXT("Animation has no MovieScene"));
    }

    FFrameRate TickRes = MovieScene->GetTickResolution();

    // Iterate all tracks
    TArray<TSharedPtr<FJsonValue>> TrackArray;
    for (UMovieSceneTrack* Track : MovieScene->GetTracks())
    {
        if (!Track) continue;

        TSharedPtr<FJsonObject> TrackObj = MakeShared<FJsonObject>();
        TrackObj->SetStringField(TEXT("track_type"), Track->GetClass()->GetName());

        // Get sections
        TArray<TSharedPtr<FJsonValue>> SectionArray;
        for (UMovieSceneSection* Section : Track->GetAllSections())
        {
            if (!Section) continue;

            TSharedPtr<FJsonObject> SecObj = MakeShared<FJsonObject>();

            TRange<FFrameNumber> SectionRange = Section->GetRange();
            if (SectionRange.HasLowerBound())
            {
                SecObj->SetNumberField(TEXT("start_time"),
                    SectionRange.GetLowerBoundValue().Value / TickRes.AsDecimal());
            }
            if (SectionRange.HasUpperBound())
            {
                SecObj->SetNumberField(TEXT("end_time"),
                    SectionRange.GetUpperBoundValue().Value / TickRes.AsDecimal());
            }

            // Count keyframes across all channels
            int32 TotalKeys = 0;
            FMovieSceneChannelProxy& ChannelProxy = Section->GetChannelProxy();
            for (const FMovieSceneChannelEntry& Entry : ChannelProxy.GetAllEntries())
            {
                for (FMovieSceneChannel* Channel : Entry.GetChannels())
                {
                    if (Channel)
                    {
                        TotalKeys += Channel->GetNumKeys();
                    }
                }
            }
            SecObj->SetNumberField(TEXT("keyframe_count"), TotalKeys);

            SectionArray.Add(MakeShared<FJsonValueObject>(SecObj));
        }
        TrackObj->SetArrayField(TEXT("sections"), SectionArray);

        TrackArray.Add(MakeShared<FJsonValueObject>(TrackObj));
    }

    // Also iterate bound object tracks
    for (const FMovieSceneBinding& Binding : MovieScene->GetBindings())
    {
        for (UMovieSceneTrack* Track : Binding.GetTracks())
        {
            if (!Track) continue;

            TSharedPtr<FJsonObject> TrackObj = MakeShared<FJsonObject>();
            TrackObj->SetStringField(TEXT("binding_name"), Binding.GetName());
            TrackObj->SetStringField(TEXT("track_type"), Track->GetClass()->GetName());

            TArray<TSharedPtr<FJsonValue>> SectionArray;
            for (UMovieSceneSection* Section : Track->GetAllSections())
            {
                if (!Section) continue;

                TSharedPtr<FJsonObject> SecObj = MakeShared<FJsonObject>();
                TRange<FFrameNumber> SectionRange = Section->GetRange();
                if (SectionRange.HasLowerBound())
                {
                    SecObj->SetNumberField(TEXT("start_time"),
                        SectionRange.GetLowerBoundValue().Value / TickRes.AsDecimal());
                }
                if (SectionRange.HasUpperBound())
                {
                    SecObj->SetNumberField(TEXT("end_time"),
                        SectionRange.GetUpperBoundValue().Value / TickRes.AsDecimal());
                }

                int32 TotalKeys = 0;
                FMovieSceneChannelProxy& ChannelProxy = Section->GetChannelProxy();
                for (const FMovieSceneChannelEntry& Entry : ChannelProxy.GetAllEntries())
                {
                    for (FMovieSceneChannel* Channel : Entry.GetChannels())
                    {
                        if (Channel)
                        {
                            TotalKeys += Channel->GetNumKeys();
                        }
                    }
                }
                SecObj->SetNumberField(TEXT("keyframe_count"), TotalKeys);

                SectionArray.Add(MakeShared<FJsonValueObject>(SecObj));
            }
            TrackObj->SetArrayField(TEXT("sections"), SectionArray);

            TrackArray.Add(MakeShared<FJsonValueObject>(TrackObj));
        }
    }

    TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
    Result->SetStringField(TEXT("asset_path"), AssetPath);
    Result->SetStringField(TEXT("animation_name"), AnimationName);
    Result->SetArrayField(TEXT("tracks"), TrackArray);
    Result->SetNumberField(TEXT("track_count"), TrackArray.Num());
    return FMonolithActionResult::Success(Result);
}

// --- create_animation ---
FMonolithActionResult FMonolithUIAnimationActions::HandleCreateAnimation(const TSharedPtr<FJsonObject>& Params)
{
    FString AssetPath = Params->GetStringField(TEXT("asset_path"));
    FString AnimationName = Params->GetStringField(TEXT("animation_name"));
    double Duration = Params->GetNumberField(TEXT("duration"));

    if (AnimationName.IsEmpty())
    {
        return FMonolithActionResult::Error(TEXT("Missing required param: animation_name"));
    }
    if (Duration <= 0.0)
    {
        return FMonolithActionResult::Error(TEXT("Duration must be > 0"));
    }

    FMonolithActionResult Err;
    UWidgetBlueprint* WBP = MonolithUIInternal::LoadWidgetBlueprint(AssetPath, Err);
    if (!WBP) return Err;

    // Check for duplicate name
    for (UWidgetAnimation* Existing : WBP->Animations)
    {
        if (Existing && Existing->GetName() == AnimationName)
        {
            return FMonolithActionResult::Error(
                FString::Printf(TEXT("Animation '%s' already exists"), *AnimationName));
        }
    }

    // Create the UWidgetAnimation as a subobject of the WBP
    UWidgetAnimation* NewAnim = NewObject<UWidgetAnimation>(
        WBP, FName(*AnimationName), RF_Transactional);

    // Create the UMovieScene
    UMovieScene* MovieScene = NewObject<UMovieScene>(
        NewAnim, FName(*(AnimationName + TEXT("_MovieScene"))), RF_Transactional);
    NewAnim->MovieScene = MovieScene;

    // Configure tick resolution and display rate
    // UE 5.7 standard: 60000 tick resolution, 30fps display
    FFrameRate TickResolution(60000, 1);
    FFrameRate DisplayRate(30, 1);
    MovieScene->SetTickResolutionDirectly(TickResolution);
    MovieScene->SetDisplayRate(DisplayRate);

    // Set playback range
    FFrameNumber StartFrame(0);
    FFrameNumber EndFrame(FMath::RoundToInt32(Duration * TickResolution.AsDecimal()));
    MovieScene->SetPlaybackRange(
        TRange<FFrameNumber>(StartFrame, EndFrame));

    int32 TrackCount = 0;
    int32 KeyframeCount = 0;

    // Process tracks array if provided
    const TArray<TSharedPtr<FJsonValue>>* TracksArray = nullptr;
    if (Params->TryGetArrayField(TEXT("tracks"), TracksArray) && TracksArray)
    {
        for (const TSharedPtr<FJsonValue>& TrackVal : *TracksArray)
        {
            const TSharedPtr<FJsonObject>* TrackObjPtr = nullptr;
            if (!TrackVal->TryGetObject(TrackObjPtr) || !TrackObjPtr || !(*TrackObjPtr).IsValid())
            {
                continue;
            }
            const TSharedPtr<FJsonObject>& TrackObj = *TrackObjPtr;

            FString WidgetName = TrackObj->GetStringField(TEXT("widget_name"));
            FString Property = TrackObj->GetStringField(TEXT("property"));

            if (WidgetName.IsEmpty() || Property.IsEmpty()) continue;

            // Find the widget in the tree
            UWidget* TargetWidget = WBP->WidgetTree
                ? WBP->WidgetTree->FindWidget(FName(*WidgetName))
                : nullptr;
            if (!TargetWidget)
            {
                continue; // Skip tracks for widgets that don't exist
            }

            // Find or create possessable for this widget
            FGuid PossessableGuid;
            bool bFoundExisting = false;
            for (int32 i = 0; i < MovieScene->GetPossessableCount(); ++i)
            {
                const FMovieScenePossessable& Poss = MovieScene->GetPossessable(i);
                if (Poss.GetName() == WidgetName)
                {
                    PossessableGuid = Poss.GetGuid();
                    bFoundExisting = true;
                    break;
                }
            }

            if (!bFoundExisting)
            {
                PossessableGuid = MovieScene->AddPossessable(
                    WidgetName, TargetWidget->GetClass());

                // Create the animation binding so UMG knows which widget this possessable maps to
                FWidgetAnimationBinding AnimBinding;
                AnimBinding.AnimationGuid = PossessableGuid;
                AnimBinding.WidgetName = FName(*WidgetName);
                AnimBinding.SlotWidgetName = NAME_None;
                NewAnim->AnimationBindings.Add(AnimBinding);
            }

            // Create a float track for the property
            if (Property == TEXT("opacity"))
            {
                // Create a float track bound to RenderOpacity
                FMovieSceneBinding* Binding = MovieScene->FindBinding(PossessableGuid);
                if (!Binding) continue;

                UMovieSceneFloatTrack* FloatTrack = MovieScene->AddTrack<UMovieSceneFloatTrack>(PossessableGuid);
                if (!FloatTrack) continue;

                FloatTrack->SetPropertyNameAndPath(
                    FName(TEXT("RenderOpacity")), TEXT("RenderOpacity"));

                // Create a section spanning the full duration
                UMovieSceneFloatSection* Section = Cast<UMovieSceneFloatSection>(
                    FloatTrack->CreateNewSection());
                if (!Section) continue;

                Section->SetRange(TRange<FFrameNumber>(StartFrame, EndFrame));
                FloatTrack->AddSection(*Section);

                // Get the float channel and add keyframes
                FMovieSceneChannelProxy& ChannelProxy = Section->GetChannelProxy();
                FMovieSceneFloatChannel* Channel = ChannelProxy.GetChannel<FMovieSceneFloatChannel>(0);
                if (!Channel) continue;

                const TArray<TSharedPtr<FJsonValue>>* KeyframesArray = nullptr;
                if (TrackObj->TryGetArrayField(TEXT("keyframes"), KeyframesArray) && KeyframesArray)
                {
                    for (const TSharedPtr<FJsonValue>& KfVal : *KeyframesArray)
                    {
                        const TSharedPtr<FJsonObject>* KfObjPtr = nullptr;
                        if (!KfVal->TryGetObject(KfObjPtr) || !KfObjPtr || !(*KfObjPtr).IsValid())
                        {
                            continue;
                        }
                        const TSharedPtr<FJsonObject>& KfObj = *KfObjPtr;

                        double Time = KfObj->GetNumberField(TEXT("time"));
                        double Value = KfObj->GetNumberField(TEXT("value"));

                        FFrameNumber KeyFrame(
                            FMath::RoundToInt32(Time * TickResolution.AsDecimal()));
                        Channel->AddLinearKey(KeyFrame, static_cast<float>(Value));
                        KeyframeCount++;
                    }
                }

                TrackCount++;
            }
            else if (Property == TEXT("transform"))
            {
                // Transform tracks use MovieScene3DTransformTrack for translation/rotation/scale
                // For UMG we use float tracks on RenderTransform sub-properties
                // Create separate float tracks for TranslationX, TranslationY, Angle, ScaleX, ScaleY

                FString SubProperties[] = {
                    TEXT("RenderTransform.Translation.X"),
                    TEXT("RenderTransform.Translation.Y"),
                    TEXT("RenderTransform.Angle"),
                    TEXT("RenderTransform.Scale.X"),
                    TEXT("RenderTransform.Scale.Y")
                };
                FString SubPropertyNames[] = {
                    TEXT("Translation X"), TEXT("Translation Y"),
                    TEXT("Angle"),
                    TEXT("Scale X"), TEXT("Scale Y")
                };

                // For transform, keyframes contain {time, tx, ty, angle, sx, sy}
                // Create one float track per sub-property, each with matching keyframes
                for (int32 SubIdx = 0; SubIdx < 5; ++SubIdx)
                {
                    UMovieSceneFloatTrack* FloatTrack = MovieScene->AddTrack<UMovieSceneFloatTrack>(PossessableGuid);
                    if (!FloatTrack) continue;

                    FloatTrack->SetPropertyNameAndPath(
                        FName(*SubPropertyNames[SubIdx]), *SubProperties[SubIdx]);

                    UMovieSceneFloatSection* Section = Cast<UMovieSceneFloatSection>(
                        FloatTrack->CreateNewSection());
                    if (!Section) continue;

                    Section->SetRange(TRange<FFrameNumber>(StartFrame, EndFrame));
                    FloatTrack->AddSection(*Section);

                    FMovieSceneChannelProxy& ChannelProxy = Section->GetChannelProxy();
                    FMovieSceneFloatChannel* Channel = ChannelProxy.GetChannel<FMovieSceneFloatChannel>(0);
                    if (!Channel) continue;

                    const TArray<TSharedPtr<FJsonValue>>* KeyframesArray = nullptr;
                    if (TrackObj->TryGetArrayField(TEXT("keyframes"), KeyframesArray) && KeyframesArray)
                    {
                        FString FieldNames[] = {
                            TEXT("tx"), TEXT("ty"), TEXT("angle"), TEXT("sx"), TEXT("sy")
                        };
                        double Defaults[] = { 0.0, 0.0, 0.0, 1.0, 1.0 };

                        for (const TSharedPtr<FJsonValue>& KfVal : *KeyframesArray)
                        {
                            const TSharedPtr<FJsonObject>* KfObjPtr = nullptr;
                            if (!KfVal->TryGetObject(KfObjPtr) || !KfObjPtr || !(*KfObjPtr).IsValid())
                            {
                                continue;
                            }
                            const TSharedPtr<FJsonObject>& KfObj = *KfObjPtr;

                            double Time = KfObj->GetNumberField(TEXT("time"));
                            double Value = Defaults[SubIdx];
                            if (KfObj->HasField(FieldNames[SubIdx]))
                            {
                                Value = KfObj->GetNumberField(FieldNames[SubIdx]);
                            }

                            FFrameNumber KeyFrame(
                                FMath::RoundToInt32(Time * TickResolution.AsDecimal()));
                            Channel->AddLinearKey(KeyFrame, static_cast<float>(Value));
                            KeyframeCount++;
                        }
                    }

                    TrackCount++;
                }
            }
            else if (Property == TEXT("color"))
            {
                // Color tracks: R, G, B, A float channels on ColorAndOpacity
                FString SubProperties[] = {
                    TEXT("ColorAndOpacity.R"),
                    TEXT("ColorAndOpacity.G"),
                    TEXT("ColorAndOpacity.B"),
                    TEXT("ColorAndOpacity.A")
                };
                FString SubPropertyNames[] = {
                    TEXT("Color R"), TEXT("Color G"), TEXT("Color B"), TEXT("Color A")
                };

                for (int32 SubIdx = 0; SubIdx < 4; ++SubIdx)
                {
                    UMovieSceneFloatTrack* FloatTrack = MovieScene->AddTrack<UMovieSceneFloatTrack>(PossessableGuid);
                    if (!FloatTrack) continue;

                    FloatTrack->SetPropertyNameAndPath(
                        FName(*SubPropertyNames[SubIdx]), *SubProperties[SubIdx]);

                    UMovieSceneFloatSection* Section = Cast<UMovieSceneFloatSection>(
                        FloatTrack->CreateNewSection());
                    if (!Section) continue;

                    Section->SetRange(TRange<FFrameNumber>(StartFrame, EndFrame));
                    FloatTrack->AddSection(*Section);

                    FMovieSceneChannelProxy& ChannelProxy = Section->GetChannelProxy();
                    FMovieSceneFloatChannel* Channel = ChannelProxy.GetChannel<FMovieSceneFloatChannel>(0);
                    if (!Channel) continue;

                    const TArray<TSharedPtr<FJsonValue>>* KeyframesArray = nullptr;
                    if (TrackObj->TryGetArrayField(TEXT("keyframes"), KeyframesArray) && KeyframesArray)
                    {
                        FString FieldNames[] = {
                            TEXT("r"), TEXT("g"), TEXT("b"), TEXT("a")
                        };
                        double Defaults[] = { 1.0, 1.0, 1.0, 1.0 };

                        for (const TSharedPtr<FJsonValue>& KfVal : *KeyframesArray)
                        {
                            const TSharedPtr<FJsonObject>* KfObjPtr = nullptr;
                            if (!KfVal->TryGetObject(KfObjPtr) || !KfObjPtr || !(*KfObjPtr).IsValid())
                            {
                                continue;
                            }
                            const TSharedPtr<FJsonObject>& KfObj = *KfObjPtr;

                            double Time = KfObj->GetNumberField(TEXT("time"));
                            double Value = Defaults[SubIdx];
                            if (KfObj->HasField(FieldNames[SubIdx]))
                            {
                                Value = KfObj->GetNumberField(FieldNames[SubIdx]);
                            }

                            FFrameNumber KeyFrame(
                                FMath::RoundToInt32(Time * TickResolution.AsDecimal()));
                            Channel->AddLinearKey(KeyFrame, static_cast<float>(Value));
                            KeyframeCount++;
                        }
                    }

                    TrackCount++;
                }
            }
            else
            {
                // Unknown property — skip with no error (lenient)
                continue;
            }
        }
    }

    // Add the animation to the widget blueprint
    WBP->Animations.Add(NewAnim);

    // Mark dirty and compile
    WBP->MarkPackageDirty();
    FKismetEditorUtilities::CompileBlueprint(WBP);

    TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
    Result->SetStringField(TEXT("asset_path"), AssetPath);
    Result->SetStringField(TEXT("animation_name"), AnimationName);
    Result->SetNumberField(TEXT("duration"), Duration);
    Result->SetNumberField(TEXT("tracks_created"), TrackCount);
    Result->SetNumberField(TEXT("keyframes_created"), KeyframeCount);
    Result->SetBoolField(TEXT("compiled"), true);
    return FMonolithActionResult::Success(Result);
}

// --- add_animation_keyframe ---
FMonolithActionResult FMonolithUIAnimationActions::HandleAddAnimationKeyframe(const TSharedPtr<FJsonObject>& Params)
{
    FString AssetPath = Params->GetStringField(TEXT("asset_path"));
    FString AnimationName = Params->GetStringField(TEXT("animation_name"));
    FString WidgetName = Params->GetStringField(TEXT("widget_name"));
    FString Property = Params->GetStringField(TEXT("property"));
    double Time = Params->GetNumberField(TEXT("time"));
    double Value = Params->GetNumberField(TEXT("value"));

    FMonolithActionResult Err;
    UWidgetBlueprint* WBP = MonolithUIInternal::LoadWidgetBlueprint(AssetPath, Err);
    if (!WBP) return Err;

    // Find the animation
    UWidgetAnimation* TargetAnim = nullptr;
    for (UWidgetAnimation* Anim : WBP->Animations)
    {
        if (Anim && Anim->GetName() == AnimationName)
        {
            TargetAnim = Anim;
            break;
        }
    }
    if (!TargetAnim)
    {
        return FMonolithActionResult::Error(
            FString::Printf(TEXT("Animation '%s' not found"), *AnimationName));
    }

    UMovieScene* MovieScene = TargetAnim->GetMovieScene();
    if (!MovieScene)
    {
        return FMonolithActionResult::Error(TEXT("Animation has no MovieScene"));
    }

    FFrameRate TickRes = MovieScene->GetTickResolution();
    FFrameNumber KeyFrame(FMath::RoundToInt32(Time * TickRes.AsDecimal()));

    // Determine the property path to match
    FString PropertyPath;
    if (Property == TEXT("opacity"))
    {
        PropertyPath = TEXT("RenderOpacity");
    }
    else
    {
        return FMonolithActionResult::Error(
            FString::Printf(TEXT("add_animation_keyframe only supports 'opacity' property. "
                "For transform/color, use create_animation with full keyframe arrays.")));
    }

    // Find the possessable for the widget
    FGuid PossessableGuid;
    bool bFound = false;
    for (int32 i = 0; i < MovieScene->GetPossessableCount(); ++i)
    {
        const FMovieScenePossessable& Poss = MovieScene->GetPossessable(i);
        if (Poss.GetName() == WidgetName)
        {
            PossessableGuid = Poss.GetGuid();
            bFound = true;
            break;
        }
    }
    if (!bFound)
    {
        return FMonolithActionResult::Error(
            FString::Printf(TEXT("No animation binding found for widget '%s'"), *WidgetName));
    }

    // Find the matching track
    FMovieSceneBinding* Binding = MovieScene->FindBinding(PossessableGuid);
    if (!Binding)
    {
        return FMonolithActionResult::Error(TEXT("No binding found for possessable"));
    }

    for (UMovieSceneTrack* Track : Binding->GetTracks())
    {
        UMovieSceneFloatTrack* FloatTrack = Cast<UMovieSceneFloatTrack>(Track);
        if (!FloatTrack) continue;

        // Check if this track's property path matches
        if (FloatTrack->GetPropertyPath().ToString() != PropertyPath) continue;

        for (UMovieSceneSection* Section : FloatTrack->GetAllSections())
        {
            FMovieSceneChannelProxy& ChannelProxy = Section->GetChannelProxy();
            FMovieSceneFloatChannel* Channel = ChannelProxy.GetChannel<FMovieSceneFloatChannel>(0);
            if (!Channel) continue;

            Channel->AddLinearKey(KeyFrame, static_cast<float>(Value));

            WBP->MarkPackageDirty();

            TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
            Result->SetStringField(TEXT("asset_path"), AssetPath);
            Result->SetStringField(TEXT("animation_name"), AnimationName);
            Result->SetStringField(TEXT("widget_name"), WidgetName);
            Result->SetStringField(TEXT("property"), Property);
            Result->SetNumberField(TEXT("time"), Time);
            Result->SetNumberField(TEXT("value"), Value);
            Result->SetBoolField(TEXT("added"), true);
            return FMonolithActionResult::Success(Result);
        }
    }

    return FMonolithActionResult::Error(
        FString::Printf(TEXT("No track found for property '%s' on widget '%s'"), *Property, *WidgetName));
}

// --- remove_animation ---
FMonolithActionResult FMonolithUIAnimationActions::HandleRemoveAnimation(const TSharedPtr<FJsonObject>& Params)
{
    FString AssetPath = Params->GetStringField(TEXT("asset_path"));
    FString AnimationName = Params->GetStringField(TEXT("animation_name"));

    FMonolithActionResult Err;
    UWidgetBlueprint* WBP = MonolithUIInternal::LoadWidgetBlueprint(AssetPath, Err);
    if (!WBP) return Err;

    // Find and remove the animation
    int32 FoundIndex = INDEX_NONE;
    FGuid AnimGuid;
    for (int32 i = 0; i < WBP->Animations.Num(); ++i)
    {
        if (WBP->Animations[i] && WBP->Animations[i]->GetName() == AnimationName)
        {
            FoundIndex = i;

            // Find the GUID from animation bindings to clean up
            UMovieScene* MovieScene = WBP->Animations[i]->GetMovieScene();
            if (MovieScene)
            {
                for (int32 p = 0; p < MovieScene->GetPossessableCount(); ++p)
                {
                    AnimGuid = MovieScene->GetPossessable(p).GetGuid();
                    break; // We just need one to identify bindings
                }
            }
            break;
        }
    }

    if (FoundIndex == INDEX_NONE)
    {
        return FMonolithActionResult::Error(
            FString::Printf(TEXT("Animation '%s' not found"), *AnimationName));
    }

    // AnimationBindings live on the UWidgetAnimation itself, not the WBP.
    // Since we're removing the animation entirely, its bindings go with it.

    // Remove the animation object
    WBP->Animations.RemoveAt(FoundIndex);

    WBP->MarkPackageDirty();
    FKismetEditorUtilities::CompileBlueprint(WBP);

    TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
    Result->SetStringField(TEXT("asset_path"), AssetPath);
    Result->SetStringField(TEXT("animation_name"), AnimationName);
    Result->SetBoolField(TEXT("removed"), true);
    Result->SetBoolField(TEXT("compiled"), true);
    return FMonolithActionResult::Success(Result);
}
