#include "Components/FINVehicleScanner.h"
#include "FGColoredInstanceMeshProxy.h"
#include "FGVehicle.h"
#include "FicsItReflection.h"
#include "FINAdvancedNetworkConnectionComponent.h"
#include "Components/BoxComponent.h"
#include "Engine/CollisionProfile.h"
#include "Engine/World.h"
#include "Engine/OverlapResult.h"
#include "Kismet/GameplayStatics.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Net/UnrealNetwork.h"
#include "Reflection/FIRClass.h"
#include "Reflection/FIRSignal.h"

namespace {
	void TriggerVehicleScannerSignal(UObject* Context, const FString& SignalName, AFGVehicle* Vehicle) {
		UFIRClass* ReflectedClass = FFicsItReflectionModule::Get().FindClass(Context->GetClass());
		if (!ReflectedClass) return;
		UFIRSignal* Signal = ReflectedClass->FindFIRSignal(SignalName);
		if (!Signal) return;
		Signal->Trigger(Context, {static_cast<FIRObj>(Vehicle)});
	}

	bool IsActorInsideScannerBox(const AActor* Actor, const FTransform& ScannerTransform, const FVector& ScannerExtent) {
		if (!Actor) return false;

		const FBox Bounds = Actor->GetComponentsBoundingBox(true);
		if (!Bounds.IsValid) {
			const FVector LocalPosition = ScannerTransform.InverseTransformPosition(Actor->GetActorLocation());
			return FMath::Abs(LocalPosition.X) <= ScannerExtent.X && FMath::Abs(LocalPosition.Y) <= ScannerExtent.Y && FMath::Abs(LocalPosition.Z) <= ScannerExtent.Z;
		}

		FVector LocalMin(TNumericLimits<float>::Max());
		FVector LocalMax(-TNumericLimits<float>::Max());
		for (int32 X = 0; X < 2; ++X) {
			for (int32 Y = 0; Y < 2; ++Y) {
				for (int32 Z = 0; Z < 2; ++Z) {
					const FVector Corner(
						X ? Bounds.Max.X : Bounds.Min.X,
						Y ? Bounds.Max.Y : Bounds.Min.Y,
						Z ? Bounds.Max.Z : Bounds.Min.Z);
					const FVector LocalCorner = ScannerTransform.InverseTransformPosition(Corner);
					LocalMin = LocalMin.ComponentMin(LocalCorner);
					LocalMax = LocalMax.ComponentMax(LocalCorner);
				}
			}
		}

		return LocalMin.X <= ScannerExtent.X && LocalMax.X >= -ScannerExtent.X &&
			LocalMin.Y <= ScannerExtent.Y && LocalMax.Y >= -ScannerExtent.Y &&
			LocalMin.Z <= ScannerExtent.Z && LocalMax.Z >= -ScannerExtent.Z;
	}
}

AFINVehicleScanner::AFINVehicleScanner() {
	NetworkConnector = CreateDefaultSubobject<UFINAdvancedNetworkConnectionComponent>(TEXT("NetworkConnector"));
	NetworkConnector->AttachToComponent(RootComponent, FAttachmentTransformRules::KeepRelativeTransform);
	NetworkConnector->SetIsReplicated(true);
	StaticMesh = CreateDefaultSubobject<UFGColoredInstanceMeshProxy>(TEXT("StaticMesh"));
	StaticMesh->AttachToComponent(RootComponent, FAttachmentTransformRules::KeepRelativeTransform);
	LampMesh= CreateDefaultSubobject<UStaticMeshComponent>(TEXT("LampMesh"));
	LampMesh->AttachToComponent(RootComponent, FAttachmentTransformRules::KeepRelativeTransform);
	VehicleCollision = CreateDefaultSubobject<UBoxComponent>(TEXT("VehicleCollision"));
	VehicleCollision->AttachToComponent(RootComponent, FAttachmentTransformRules::KeepRelativeTransform);
	VehicleCollision->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	VehicleCollision->SetCollisionResponseToAllChannels(ECR_Overlap);
	VehicleCollision->SetGenerateOverlapEvents(true);
	VehicleCollision->OnComponentBeginOverlap.AddDynamic(this, &AFINVehicleScanner::OnVehicleCollisionBeginOverlap);
	VehicleCollision->OnComponentEndOverlap.AddDynamic(this, &AFINVehicleScanner::OnVehicleCollisionEndOverlap);

	PrimaryActorTick.bCanEverTick = true;
	SetActorTickEnabled(true);
}

void AFINVehicleScanner::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const {
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	
	DOREPLIFETIME(AFINVehicleScanner, ScanColor);
	DOREPLIFETIME(AFINVehicleScanner, Intensity);
}

void AFINVehicleScanner::Tick(float DeltaSeconds) {
	Super::Tick(DeltaSeconds);

	PollVehicleCollision();

	if (bColorChanged) {
		bColorChanged = false;
		Client_OnColorChanged();
	}
}

void AFINVehicleScanner::BeginPlay() {
	Super::BeginPlay();
	PrimaryActorTick.bCanEverTick = true;
	SetActorTickEnabled(true);
	if (VehicleCollision) {
		VehicleCollision->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
		VehicleCollision->SetCollisionResponseToAllChannels(ECR_Overlap);
		VehicleCollision->SetGenerateOverlapEvents(true);
		VehicleCollision->OnComponentBeginOverlap.RemoveDynamic(this, &AFINVehicleScanner::OnVehicleCollisionBeginOverlap);
		VehicleCollision->OnComponentEndOverlap.RemoveDynamic(this, &AFINVehicleScanner::OnVehicleCollisionEndOverlap);
		VehicleCollision->OnComponentBeginOverlap.AddDynamic(this, &AFINVehicleScanner::OnVehicleCollisionBeginOverlap);
		VehicleCollision->OnComponentEndOverlap.AddDynamic(this, &AFINVehicleScanner::OnVehicleCollisionEndOverlap);
	}
	if (LampMesh->GetMaterials().Num() > 0) {
		int matIdx = LampMesh->GetMaterialIndex("CustomEmissive_Scanner");
		LightMaterialInstance = LampMesh->CreateDynamicMaterialInstance(matIdx);
		LampMesh->SetMaterial(matIdx, LightMaterialInstance);
	}
	UpdateColor();
}

void AFINVehicleScanner::OnVehicleCollisionBeginOverlap(UPrimitiveComponent* OverlappedComponent, AActor* OtherActor, UPrimitiveComponent* OtherComp, int32 OtherBodyIndex, bool bFromSweep, const FHitResult& SweepResult) {
	AFGVehicle* Vehicle = Cast<AFGVehicle>(OtherActor);
	if (Vehicle) HandleVehicleEnter(Vehicle);
}

void AFINVehicleScanner::OnVehicleCollisionEndOverlap(UPrimitiveComponent* OverlappedComponent, AActor* OtherActor, UPrimitiveComponent* OtherComp, int32 OtherBodyIndex) {
	AFGVehicle* Vehicle = Cast<AFGVehicle>(OtherActor);
	if (Vehicle) HandleVehicleExit(Vehicle);
}

void AFINVehicleScanner::PollVehicleCollision() {
	if (!IsInGameThread()) return;
	if (!VehicleCollision || !GetWorld()) return;

	TArray<FOverlapResult> Overlaps;
	FCollisionObjectQueryParams ObjectTypes;
	ObjectTypes.AddObjectTypesToQuery(ECC_Pawn);
	ObjectTypes.AddObjectTypesToQuery(ECC_WorldDynamic);
	FCollisionQueryParams QueryParams(SCENE_QUERY_STAT(FINVehicleScanner), false, this);

	const FTransform Transform = VehicleCollision->GetComponentTransform();
	FVector Extent = VehicleCollision->GetScaledBoxExtent();
	if (Extent.IsNearlyZero()) Extent = FVector(800.0f, 800.0f, 800.0f);

	GetWorld()->OverlapMultiByObjectType(Overlaps, Transform.GetLocation(), Transform.GetRotation(), ObjectTypes, FCollisionShape::MakeBox(Extent), QueryParams);

	TSet<TWeakObjectPtr<AFGVehicle>> NewVehicles;
	for (const FOverlapResult& Overlap : Overlaps) {
		AFGVehicle* Vehicle = Cast<AFGVehicle>(Overlap.GetActor());
		if (Vehicle) {
			NewVehicles.Add(Vehicle);
			if (!OverlappingVehicles.Contains(Vehicle)) HandleVehicleEnter(Vehicle);
		}
	}

	TArray<AActor*> Vehicles;
	UGameplayStatics::GetAllActorsOfClass(GetWorld(), AFGVehicle::StaticClass(), Vehicles);
	for (AActor* Actor : Vehicles) {
		AFGVehicle* Vehicle = Cast<AFGVehicle>(Actor);
		if (!Vehicle) continue;
		if (IsActorInsideScannerBox(Vehicle, Transform, Extent)) {
			NewVehicles.Add(Vehicle);
			if (!OverlappingVehicles.Contains(Vehicle)) HandleVehicleEnter(Vehicle);
		}
	}

	for (const TWeakObjectPtr<AFGVehicle>& Vehicle : OverlappingVehicles) {
		if (Vehicle.IsValid() && !NewVehicles.Contains(Vehicle)) HandleVehicleExit(Vehicle.Get());
	}
	OverlappingVehicles = NewVehicles;
	if (OverlappingVehicles.IsEmpty()) LastVehicle = nullptr;
}

void AFINVehicleScanner::HandleVehicleEnter(AFGVehicle* Vehicle) {
	if (!Vehicle || OverlappingVehicles.Contains(Vehicle)) return;
	OverlappingVehicles.Add(Vehicle);
	TriggerVehicleScannerSignal(this, TEXT("OnVehicleEnter"), Vehicle);
	LastVehicle = Vehicle;
}

void AFINVehicleScanner::HandleVehicleExit(AFGVehicle* Vehicle) {
	if (!Vehicle || !OverlappingVehicles.Contains(Vehicle)) return;
	OverlappingVehicles.Remove(Vehicle);
	TriggerVehicleScannerSignal(this, TEXT("OnVehicleExit"), Vehicle);
	if (OverlappingVehicles.IsEmpty()) LastVehicle = nullptr;
}

UObject* AFINVehicleScanner::GetSignalSenderOverride_Implementation() {
	return this;
}

void AFINVehicleScanner::Client_OnColorChanged_Implementation() {
	UpdateColor();
}

void AFINVehicleScanner::UpdateColor_Implementation() {
	if (LightMaterialInstance) {
		LightMaterialInstance->SetVectorParameterValue("Color", ScanColor);
		LightMaterialInstance->SetScalarParameterValue("Intensity", Intensity);
	}
}

void AFINVehicleScanner::netFunc_setColor(float r, float g, float b, float e) {
	ScanColor = FLinearColor(FMath::Clamp(r, 0.0f, 1.0f), FMath::Clamp(g, 0.0f, 1.0f), FMath::Clamp(b, 0.0f, 1.0f));
	Intensity = FMath::Clamp(e, 0.0f, 5.0f);
	bColorChanged = true;
}

void AFINVehicleScanner::netFunc_getColor(float& r, float& g, float& b, float& e) {
	r = ScanColor.R;
	g = ScanColor.G;
	b = ScanColor.B;
	e = Intensity;
}

AFGVehicle* AFINVehicleScanner::netFunc_getLastVehicle() {
	PollVehicleCollision();
	return LastVehicle;
}

void AFINVehicleScanner::netFunc_getDebug(int32& vehicleCount, float& nearestVehicleDistance, bool& nearestVehicleInside, FString& nearestVehicleClass, int32& actorCount, float& nearestActorDistance, FString& nearestActorClass, float& boxX, float& boxY, float& boxZ) {
	PollVehicleCollision();

	vehicleCount = 0;
	nearestVehicleDistance = -1.0f;
	nearestVehicleInside = false;
	nearestVehicleClass = TEXT("");
	actorCount = 0;
	nearestActorDistance = -1.0f;
	nearestActorClass = TEXT("");
	boxX = 0.0f;
	boxY = 0.0f;
	boxZ = 0.0f;

	if (!IsInGameThread()) return;
	if (!GetWorld()) return;

	FTransform ScannerTransform = GetActorTransform();
	FVector Extent = FVector(800.0f, 800.0f, 800.0f);
	if (VehicleCollision) {
		ScannerTransform = VehicleCollision->GetComponentTransform();
		Extent = VehicleCollision->GetScaledBoxExtent();
		if (Extent.IsNearlyZero()) Extent = FVector(800.0f, 800.0f, 800.0f);
	}

	boxX = Extent.X;
	boxY = Extent.Y;
	boxZ = Extent.Z;

	const FVector ScannerLocation = ScannerTransform.GetLocation();
	TArray<AActor*> Vehicles;
	UGameplayStatics::GetAllActorsOfClass(GetWorld(), AFGVehicle::StaticClass(), Vehicles);
	vehicleCount = Vehicles.Num();
	for (AActor* Actor : Vehicles) {
		if (!Actor) continue;
		const float Distance = FVector::Dist(ScannerLocation, Actor->GetActorLocation());
		if (nearestVehicleDistance < 0.0f || Distance < nearestVehicleDistance) {
			nearestVehicleDistance = Distance;
			nearestVehicleClass = Actor->GetClass() ? Actor->GetClass()->GetName() : TEXT("");
			nearestVehicleInside = IsActorInsideScannerBox(Actor, ScannerTransform, Extent);
		}
	}

	TArray<AActor*> Actors;
	UGameplayStatics::GetAllActorsOfClass(GetWorld(), AActor::StaticClass(), Actors);
	actorCount = Actors.Num();
	for (AActor* Actor : Actors) {
		if (!Actor || Actor == this) continue;
		const float Distance = FVector::Dist(ScannerLocation, Actor->GetActorLocation());
		if (nearestActorDistance < 0.0f || Distance < nearestActorDistance) {
			nearestActorDistance = Distance;
			nearestActorClass = Actor->GetClass() ? Actor->GetClass()->GetName() : TEXT("");
		}
	}
}

void AFINVehicleScanner::netSig_OnVehicleExit_Implementation(AFGVehicle* Vehicle) {}
void AFINVehicleScanner::netSig_OnVehicleEnter_Implementation(AFGVehicle* Vehicle) {}

