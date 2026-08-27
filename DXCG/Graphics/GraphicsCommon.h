#pragma once

// 프레임 리소스를 몇 벌 돌려쓸지. 상수 버퍼가 이 개수만큼 복제되고,
// NumFramesDirty도 이 값에서 시작해 프레임마다 감소한다.
static const int MaxFrameResource = 3;

// LightingUtils.hlsl의 MaxLights와 반드시 일치해야 함
#define MAXLIGHT 16

// Default.hlsl의 NUM_DIR_LIGHTS / NUM_POINT_LIGHTS / NUM_SPOT_LIGHTS와 일치해야 함.
// gLights 배열에서 각 타입이 차지하는 슬롯 범위를 결정한다.
static const int NumDirLights = 1;
static const int NumPointLights = 1;
static const int NumSpotLights = 0;
