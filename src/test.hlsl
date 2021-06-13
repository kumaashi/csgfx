uint4 info: register(b0);
RWTexture2D<float4> tex0 : register(u0);
RWTexture2D<float4> tex1 : register(u1);

float map(float3 p) {
	float time = info.w / 60.0;
	//return length(abs(p % 1) - 0.5) - 0.15;
	p.x += sin(p.z * 0.3) * 0.5;
	p.y += cos(p.z * 0.3) * 0.75;
	return 1.0 - length(p.xy);
}


float3 getnor(float3 p) {
	float2 d = float2(0.001, 0.0);
	float t = map(p);
	return normalize(float3(
		t - map(p + d.xyy),
		t - map(p + d.yxy),
		t - map(p + d.yyx)));
}


float4 getcol(float2 vp) {
	float time = info.w / 60.0;
	float2 uv = (2.0 * vp - info.xy) / min(info.x, info.y);
	float3 dir = normalize(float3(uv, 1.0));
	float3 pos = float3(0, 0, time);

	float t = 0.0;
	for(int i = 0; i < 64; i++) {
		t += map(dir * t + pos);
	}
	float3 ip = dir * t + pos;
	float3 N = getnor(ip);
	float3 L = normalize(float3(1,2,3));
	float D = max(0.01, dot(N, L));
	return float4(abs(N), 1) * D + t * 0.25;
}

[numthreads(8, 8, 1)]
void CSMain(
	uint3 gid : SV_GroupID,
	uint3 gtid : SV_GroupThreadID,
	uint3 dtid : SV_DispatchThreadID,
	uint gidx : SV_GroupIndex)
{
	tex0[dtid.xy] = getcol(float2(dtid.xy));
}

