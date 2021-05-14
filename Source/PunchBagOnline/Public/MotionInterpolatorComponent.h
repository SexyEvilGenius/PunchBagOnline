#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "MotionInterpolatorComponent.generated.h"

UENUM()
enum class EMotionSnapshotFlags : uint8
{
	None				= 0x0,
	HasVelocity			= 0x1,
	HasAngularVelocity	= 0x2,

	FLAGS_COUNT			= 0x2
};
ENUM_CLASS_FLAGS(EMotionSnapshotFlags)

USTRUCT(BlueprintType)
struct FMotionSnapshot
{
	GENERATED_USTRUCT_BODY()

public:
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MotionInterpolator")
	FVector Location;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MotionInterpolator")
	FQuat Rotation;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MotionInterpolator")
	FVector Velocity;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MotionInterpolator")
	FVector AngularVelocity;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MotionInterpolator")
	float Timestamp;

	FMotionSnapshot(FVector InLocation, FQuat InRotation, FVector InVelocity, FVector InAngularVelocity, float InTimestamp) :
		Location(InLocation),
		Rotation(InRotation),
		Velocity(InVelocity),
		AngularVelocity(InAngularVelocity),
		Timestamp(InTimestamp)
	{}

	FMotionSnapshot() :
		Location(),
		Rotation(),
		Velocity(),
		AngularVelocity(),
		Timestamp()
	{}

	FMotionSnapshot(const USceneComponent& InComponent, float InTimestamp) :
		Location(InComponent.GetComponentLocation()),
		Rotation(InComponent.GetComponentRotation()),
		Velocity(InComponent.GetComponentVelocity()),
		Timestamp(InTimestamp)
	{
		const UPrimitiveComponent* asPrimitiveComponent = Cast<const UPrimitiveComponent>(&InComponent);
		if (IsValid(asPrimitiveComponent))
		{
			AngularVelocity = asPrimitiveComponent->GetPhysicsAngularVelocityInDegrees();
		}
	}

	void ApplyTo(USceneComponent& InComponent)
	{
		InComponent.SetWorldLocationAndRotation(Location, Rotation);
		UPrimitiveComponent* asPrimitiveComponent = Cast<UPrimitiveComponent>(&InComponent);
		if (IsValid(asPrimitiveComponent))
		{
			asPrimitiveComponent->SetPhysicsLinearVelocity(Velocity);
			asPrimitiveComponent->SetPhysicsAngularVelocityInDegrees(AngularVelocity);
		}
	}

	bool NetSerialize(FArchive& Ar, class UPackageMap* Map, bool& bOutSuccess);
};

template<>
struct TStructOpsTypeTraits<FMotionSnapshot> : public TStructOpsTypeTraitsBase2<FMotionSnapshot>
{
	enum
	{
		WithNetSerializer = true
	};
};

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FMotionInterpolatorDelegate, const FMotionSnapshot&, Snapshot);

UCLASS( ClassGroup=(Custom), meta=(BlueprintSpawnableComponent) )
class PUNCHBAGONLINE_API UMotionInterpolatorComponent : public UActorComponent
{
	GENERATED_BODY()

public:	
	UMotionInterpolatorComponent();

	virtual void TickComponent(float DeltaTime, enum ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

	UFUNCTION(BlueprintCallable, Category = "MotionInterpolator")
	void SetComponentOverride(class USceneComponent* InComponentOverride);

	UFUNCTION(BlueprintCallable, Category = "MotionInterpolator")
	void AddSnapshot(const FMotionSnapshot& Snapshot);
	UFUNCTION(BlueprintCallable, Category = "MotionInterpolator")
	void GetSnapshotAtTime(float TargetTime, bool CanExtrapolate, FMotionSnapshot& Result, int& OffBorder);

	UFUNCTION(Server, unreliable)
	void ServerSendSnapshot(const FMotionSnapshot& InSnapshot, FGuid SenderGuid);
	UFUNCTION(NetMulticast, unreliable)
	void MulticastSendSnapshot(const FMotionSnapshot& InSnapshot, FGuid SenderGuid);

	UFUNCTION(BlueprintPure, Category = "MotionInterpolator")
	TArray<FMotionSnapshot> GetSnapshots() { return Snapshots; }

	UFUNCTION(BlueprintCallable, Category = "MotionInterpolator")
	void OvertakeMovementAuthority(float Duration, float BlendOutDuration);

	FMotionSnapshot Interpolate(const FMotionSnapshot& FirstSnapshot, const FMotionSnapshot& SecondSnapshot, float TargetTime);
	FMotionSnapshot SimpleInterpolate(const FMotionSnapshot& FirstSnapshot, const FMotionSnapshot& SecondSnapshot, float Alpha);
	FMotionSnapshot Extrapolate(const FMotionSnapshot& Snapshot, float TargetTime);

	UPROPERTY(BlueprintAssignable, Category = "MotionInterpolator")
	FMotionInterpolatorDelegate OnSnapshotAdded;

	UPROPERTY(EditAnywhere, Category = "MotionInterpolator")
	bool UseExtrapolation = false;

	UPROPERTY(EditAnywhere, Category = "MotionInterpolator")
	FName SyncedComponentName = NAME_None;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MotionInterpolator", meta = (ExposeOnSpawn = true))
	int BufferSize = 10;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MotionInterpolator")
	float NetworkDelay = 0.1f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MotionInterpolator")
	float SyncPeriod = 0.1f;
	
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MotionInterpolator")
	float SnapPeriod = 0.2f;

private:
	class USceneComponent* GetComponentToSync();

	class USceneComponent* ComponentToSync;
	class USceneComponent* ComponentOverride;
	TArray<FMotionSnapshot> Snapshots;
	FGuid GUID = FGuid::NewGuid();
	float AuthorityReleaseTime = 0.0f;
	float AuthorityBlendOutFinishTime = 0.0f;
	float AuthorityBlendOutDuration = 0.0f;
	float LastSyncTime = 0.0f;
	float LastSnapTime = 0.0f;
};