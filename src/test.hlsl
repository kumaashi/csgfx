uint4 info: register(b0);
RWTexture2D<float4> tex0 : register(u0);
RWTexture2D<float4> tex1 : register(u1);

float map(float3 p) {
	float time = info.w / 60.0;
	float t = length(abs(p % 1) - 0.5) - 0.2;
	t = min(t, length(abs(p.xy % 1) - 0.5) - 0.02);
	t = min(t, length(abs(p.yz % 1) - 0.5) - 0.02);
	t = min(t, length(abs(p.zx % 1) - 0.5) - 0.02);
	return t;
}


float3 getnor(float3 p) {
	float2 d = float2(0.001, 0.0);
	float t = map(p);
	return normalize(float3(
		t - map(p + d.xyy),
		t - map(p + d.yxy),
		t - map(p + d.yyx)));
}


float2 rot(float2 p, float a) {
	return float2(
		p.x * cos(a) - p.y * sin(a),
		p.x * sin(a) + p.y * cos(a));
}

float4 getcol(float2 vp) {
	float time = info.w / 60.0;
	float tm = time * 0.25;
	float2 uv = (2.0 * vp - info.xy) / min(info.x, info.y);
	float3 dir = normalize(float3(uv, 1.0));
	dir.xz = rot(dir.xz, floor(tm * 0.50) + smoothstep(0.25, 0.75, frac(tm * 0.50)));
	dir.zy = rot(dir.zy, floor(tm * 0.25) + smoothstep(0.25, 0.75, frac(tm * 0.25)));
	float3 pos = float3(time * 0.25, 0, time);

	float t = 0.0;
	for(int i = 0; i < 100; i++) {
		t += map(dir * t + pos);
	}
	float3 ip = dir * t + pos;
	float3 L = normalize(float3(1,2,3));
	float3 N = getnor(ip);
	float3 esV = normalize(ip - pos);
	float D = max(0.01, dot(N, L));
	float S = max(0.01, pow(dot(N, esV), 16.0) * 0.5);
	return float4(abs(N) * S + abs(N) * D, 1) + t * 0.05;
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

