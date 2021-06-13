#include <windows.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdio.h>
#include <hidusage.h>
#include <vector>
#include <algorithm>
#include <map>
#include <string>

#include <D3Dcompiler.h>
#include <d3d12.h>
#include <dxgi1_4.h>

#pragma comment(lib, "kernel32.lib")
#pragma comment(lib, "user32.lib")
#pragma comment(lib, "gdi32.lib")
#pragma comment(lib, "winmm.lib")
#pragma comment(lib, "dxgi.lib")
#pragma comment(lib, "d3d12.lib")
#pragma comment(lib, "D3DCompiler.lib")

static LRESULT WINAPI
msg_proc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
	switch (msg) {
	case WM_SYSCOMMAND:
		switch ((wParam & 0xFFF0)) {
		case SC_MONITORPOWER:
		case SC_SCREENSAVE:
			return 0;
		default:
			break;
		}
		break;
	case WM_CLOSE:
	case WM_DESTROY:
		PostQuitMessage(0);
		return 0;
	case WM_IME_SETCONTEXT:
		lParam &= ~ISC_SHOWUIALL;
		break;
	case WM_KEYDOWN:
		if (wParam == VK_ESCAPE)
			PostQuitMessage(0);
		break;
	}
	return DefWindowProc(hWnd, msg, wParam, lParam);
}


static HWND
init_window(const char *name, int w, int h)
{
	auto instance = GetModuleHandle(NULL);
	auto style = WS_OVERLAPPEDWINDOW & ~WS_MAXIMIZEBOX & ~WS_THICKFRAME;
	auto ex_style = WS_EX_APPWINDOW | WS_EX_WINDOWEDGE;
	RECT rc = {0, 0, w, h};
	WNDCLASSEX twc = {
		sizeof(WNDCLASSEX), CS_CLASSDC, msg_proc, 0L, 0L, instance,
		LoadIcon(NULL, IDI_APPLICATION), LoadCursor(NULL, IDC_ARROW),
		(HBRUSH)GetStockObject(BLACK_BRUSH), NULL, name, NULL
	};

	RegisterClassEx(&twc);
	AdjustWindowRectEx(&rc, style, FALSE, ex_style);
	rc.right -= rc.left;
	rc.bottom -= rc.top;
	auto hwnd = CreateWindowEx(
			ex_style, name, name, style,
			(GetSystemMetrics(SM_CXSCREEN) - rc.right) / 2,
			(GetSystemMetrics(SM_CYSCREEN) - rc.bottom) / 2,
			rc.right, rc.bottom, NULL, NULL, instance, NULL);
	ShowWindow(hwnd, SW_SHOW);
	SetFocus(hwnd);
	return (hwnd);
};

bool
update_window()
{
	MSG msg;
	bool is_active = true;

	while (PeekMessage(&msg, 0, 0, 0, PM_REMOVE)) {
		if (msg.message == WM_QUIT) {
			is_active = false;
			break;
		} else {
			TranslateMessage(&msg);
			DispatchMessage(&msg);
		}
	}
	return is_active;
}

D3D12_SHADER_BYTECODE
create_shader_from_file(
	std::string fstr, std::string entry, std::string profile,
	std::vector<uint8_t> &shader_code)
{
	ID3DBlob *blob = nullptr;
	ID3DBlob *blob_err = nullptr;
	ID3DBlob *blob_sig = nullptr;

	std::vector<WCHAR> wfname;
	UINT flags = 0;
	for (int i = 0; i < fstr.length(); i++)
		wfname.push_back(fstr[i]);
	wfname.push_back(0);
	D3DCompileFromFile(&wfname[0], NULL, D3D_COMPILE_STANDARD_FILE_INCLUDE,
		entry.c_str(), profile.c_str(), flags, 0, &blob, &blob_err);
	if (blob_err) {
		printf("%s:\n%s\n", __FUNCTION__, (char *) blob_err->GetBufferPointer());
		blob_err->Release();
	}
	if (!blob && !blob_err)
		printf("File Not found : %s\n", fstr.c_str());
	if (!blob)
		return {nullptr, 0};
	shader_code.resize(blob->GetBufferSize());
	memcpy(shader_code.data(), blob->GetBufferPointer(), blob->GetBufferSize());
	if (blob)
		blob->Release();
	return { shader_code.data(), shader_code.size() };
}


struct dx12heap {
	ID3D12DescriptorHeap *heap = NULL;
	uint64_t index = 0;
	std::map<void *, D3D12_CPU_DESCRIPTOR_HANDLE> mhcpu;
	std::map<void *, D3D12_GPU_DESCRIPTOR_HANDLE> mhgpu;

	void init(ID3D12Device *dev, D3D12_DESCRIPTOR_HEAP_TYPE type, UINT num = 256)
	{
		auto flag = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
		if (type == D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV)
			flag = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
		dev->CreateDescriptorHeap(&D3D12_DESCRIPTOR_HEAP_DESC({type, num, flag, 0}), IID_PPV_ARGS(&heap));
	}

	void alloc(ID3D12Device *dev, void *res)
	{
		auto desc = heap->GetDesc();
		auto inc_size = dev->GetDescriptorHandleIncrementSize(desc.Type);
		auto hcpu = heap->GetCPUDescriptorHandleForHeapStart();
		auto hgpu = heap->GetGPUDescriptorHandleForHeapStart();
		hcpu.ptr += inc_size * index;
		hgpu.ptr += inc_size * index;
		mhcpu[res] = hcpu;
		mhgpu[res] = hgpu;
		index++;
	}

	D3D12_CPU_DESCRIPTOR_HANDLE
	get_hcpu(ID3D12Device *dev, void *res)
	{
		if (mhcpu.count(res) == 0)
			alloc(dev, res);
		return mhcpu[res];
	}

	D3D12_GPU_DESCRIPTOR_HANDLE
	get_hgpu(ID3D12Device *dev, void *res)
	{
		if (mhgpu.count(res) == 0)
			alloc(dev, res);
		return mhgpu[res];
	}
};

struct dx12device {
	ID3D12Device *dev = NULL;
	ID3D12CommandQueue *queue = NULL;
	IDXGISwapChain3 *swap_chain = NULL;

	void
	init(HWND hwnd, UINT Width, UINT Height, UINT BufferCount)
	{
		D3D12_COMMAND_QUEUE_DESC desc = {};
		D3D12CreateDevice(NULL, D3D_FEATURE_LEVEL_12_1, IID_PPV_ARGS(&dev));
		printf("dev=%p\n", dev);
		dev->CreateCommandQueue(&desc, IID_PPV_ARGS(&queue));
		printf("queue=%p\n", queue);
		{
			IDXGIFactory4 *factory = NULL;
			IDXGISwapChain *temp = NULL;
			DXGI_SWAP_CHAIN_DESC desc = {
				{
					Width, Height, {0, 0},
					DXGI_FORMAT_R8G8B8A8_UNORM,
					DXGI_MODE_SCANLINE_ORDER_UNSPECIFIED,
					DXGI_MODE_SCALING_UNSPECIFIED
				},
				{1, 0},
				DXGI_USAGE_RENDER_TARGET_OUTPUT,
				BufferCount, hwnd, TRUE, DXGI_SWAP_EFFECT_FLIP_DISCARD,
				DXGI_SWAP_CHAIN_FLAG_FRAME_LATENCY_WAITABLE_OBJECT
			};
			CreateDXGIFactory1(IID_PPV_ARGS(&factory));
			factory->MakeWindowAssociation(hwnd, DXGI_MWA_NO_ALT_ENTER);
			factory->CreateSwapChain(queue, &desc, &temp);
			temp->QueryInterface(IID_PPV_ARGS(&swap_chain));
			temp->Release();
			factory->Release();
		}
	}
};

struct dx12context {
	ID3D12CommandAllocator *cmdalloc = NULL;
	ID3D12GraphicsCommandList *cmdlist = NULL;
	ID3D12Resource *back_buffer = NULL;
	ID3D12Fence *fence = NULL;
	uint64_t fence_value = -1;
	void init(ID3D12Device *dev, IDXGISwapChain3 *swap_chain, UINT buffer_index)
	{
		fence_value = -1;
		dev->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&cmdalloc));
		dev->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, cmdalloc, NULL, IID_PPV_ARGS(&cmdlist));
		dev->CreateFence(fence_value, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&fence));
		swap_chain->GetBuffer(buffer_index, IID_PPV_ARGS(&back_buffer));
	}

};

D3D12_RESOURCE_BARRIER
get_res_barrier(ID3D12Resource *res,
	D3D12_RESOURCE_STATES before, D3D12_RESOURCE_STATES after,
	D3D12_RESOURCE_BARRIER_TYPE type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION)
{
	D3D12_RESOURCE_BARRIER ret = {};
	ret.Type = type;
	ret.Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
	ret.Transition.pResource = res;
	ret.Transition.StateBefore = before;
	ret.Transition.StateAfter = after;
	ret.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
	return ret;
}

ID3D12Resource *
create_res(ID3D12Device *dev, int w, int h, DXGI_FORMAT fmt,
	D3D12_RESOURCE_FLAGS flags, D3D12_HEAP_TYPE htype = D3D12_HEAP_TYPE_DEFAULT)
{
	ID3D12Resource *res = NULL;
	D3D12_RESOURCE_DESC desc = {
		D3D12_RESOURCE_DIMENSION_TEXTURE2D, 0, UINT64(w), UINT(h), 1, 1, fmt,
		{1, 0}, D3D12_TEXTURE_LAYOUT_UNKNOWN, flags
	};
	D3D12_HEAP_PROPERTIES hprop = {
		D3D12_HEAP_TYPE_DEFAULT,
		D3D12_CPU_PAGE_PROPERTY_UNKNOWN,
		D3D12_MEMORY_POOL_UNKNOWN, 1, 1,
	};
	desc.MipLevels = 1;
	hprop.Type = htype;
	auto state = D3D12_RESOURCE_STATE_COMMON;
	if (hprop.Type == D3D12_HEAP_TYPE_UPLOAD) {
		desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
		desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
		desc.Format = DXGI_FORMAT_UNKNOWN;
		desc.MipLevels = 1;
		state = D3D12_RESOURCE_STATE_GENERIC_READ;
	}
	auto hr = dev->CreateCommittedResource(&hprop, D3D12_HEAP_FLAG_NONE, &desc, state, NULL, IID_PPV_ARGS(&res));
	return res;
}

void
create_rtv(ID3D12Device *dev, ID3D12Resource *res,
	D3D12_CPU_DESCRIPTOR_HANDLE hcpu)
{
	D3D12_RENDER_TARGET_VIEW_DESC desc = {};
	D3D12_RESOURCE_DESC desc_res = res->GetDesc();
	desc.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2D;
	desc.Format = desc_res.Format;
	dev->CreateRenderTargetView(res, &desc, hcpu);
}

void
create_srv(ID3D12Device *dev, ID3D12Resource *res,
	D3D12_CPU_DESCRIPTOR_HANDLE hcpu)
{
	D3D12_SHADER_RESOURCE_VIEW_DESC desc = {};
	D3D12_RESOURCE_DESC desc_res = res->GetDesc();
	desc.Format = desc_res.Format;
	desc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
	desc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
	desc.Texture2D.MipLevels = 1;
	dev->CreateShaderResourceView(res, &desc, hcpu);
}

void
create_uav(ID3D12Device *dev, ID3D12Resource *res,
	UINT num_elem, UINT byte_stride,
	D3D12_CPU_DESCRIPTOR_HANDLE hcpu)
{
	D3D12_UNORDERED_ACCESS_VIEW_DESC desc = {};
	D3D12_RESOURCE_DESC desc_res = res->GetDesc();

	desc.Format = desc_res.Format;
	if (byte_stride == 0) {
		desc.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;
		desc.Texture2D.MipSlice = 0;
		desc.Texture2D.PlaneSlice = 0;
	} else {
		desc.ViewDimension = D3D12_UAV_DIMENSION_BUFFER;
		desc.Buffer.FirstElement = 0;
		desc.Buffer.NumElements = num_elem;
		desc.Buffer.StructureByteStride = byte_stride;
		desc.Buffer.CounterOffsetInBytes = 0;
	}
	dev->CreateUnorderedAccessView(res, NULL, &desc, hcpu);
}

int
create_cbv(ID3D12Device *dev, ID3D12Resource *res,
	D3D12_CPU_DESCRIPTOR_HANDLE hcpu_cbv)
{
	D3D12_RESOURCE_DESC desc_res = res->GetDesc();
	D3D12_CONSTANT_BUFFER_VIEW_DESC desc_cbv = {};

	desc_cbv.SizeInBytes = desc_res.Width;
	desc_cbv.BufferLocation = res->GetGPUVirtualAddress();
	dev->CreateConstantBufferView(&desc_cbv, hcpu_cbv);
	return (0);
}

int
trans_data(ID3D12Device *dev, ID3D12GraphicsCommandList *cmdlist,
	int subres_index, ID3D12Resource *res_dest, ID3D12Resource *res_src)
{
	D3D12_PLACED_SUBRESOURCE_FOOTPRINT footprint;
	D3D12_TEXTURE_COPY_LOCATION dest = {};
	D3D12_TEXTURE_COPY_LOCATION src = {};
	D3D12_RESOURCE_BARRIER barrier = {};
	D3D12_RESOURCE_DESC desc = res_dest->GetDesc();
	UINT64 total_bytes = 0;
	auto barrier_start = get_res_barrier(res_dest, D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_COPY_DEST);
	auto barrier_end = get_res_barrier(res_dest, D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_COMMON);

	dev->GetCopyableFootprints(&desc, subres_index, 1, 0, &footprint, NULL, NULL, &total_bytes);
	dest.pResource = res_dest;
	dest.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
	dest.SubresourceIndex = subres_index;
	src.pResource = res_src;
	src.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
	src.PlacedFootprint = footprint;

	cmdlist->ResourceBarrier(1, &barrier_start);
	cmdlist->CopyTextureRegion( &dest, 0, 0, 0, &src, NULL );
	cmdlist->ResourceBarrier(1, &barrier_end);

	return (0);
}

int
copy_res_data(ID3D12Device *dev, ID3D12GraphicsCommandList *cmdlist, ID3D12Resource *res_dest, ID3D12Resource *res_src)
{
	auto barrier_dest_start = get_res_barrier(res_dest, D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_COPY_DEST);
	auto barrier_dest_end = get_res_barrier(res_dest, D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_COMMON);
	auto barrier_src_start = get_res_barrier(res_src, D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_COPY_SOURCE);
	auto barrier_src_end = get_res_barrier(res_src, D3D12_RESOURCE_STATE_COPY_SOURCE, D3D12_RESOURCE_STATE_COMMON);

	std::vector<D3D12_RESOURCE_BARRIER> vstart = { barrier_dest_start, barrier_src_start, };
	std::vector<D3D12_RESOURCE_BARRIER> vend = { barrier_dest_end, barrier_src_end, };

	cmdlist->ResourceBarrier(vstart.size(), vstart.data());
	cmdlist->CopyResource(res_dest, res_src);
	cmdlist->ResourceBarrier(vend.size(), vend.data());
	return (0);
}

ID3D12RootSignature *
create_rootsig(ID3D12Device *dev, UINT slot)
{
	ID3D12RootSignature *rootsig = NULL;
	ID3DBlob *perrblob = NULL;
	ID3DBlob *signature = NULL;
	D3D12_ROOT_SIGNATURE_DESC root_signature_desc = {};
	D3D12_ROOT_PARAMETER root_param = {};
	std::vector<D3D12_ROOT_PARAMETER> vroot_param;
	std::vector<D3D12_DESCRIPTOR_RANGE> vdesc_range;

	for (UINT i = 0 ; i < slot; i++) {
		vdesc_range.push_back({D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, i, 0, D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND});
		vdesc_range.push_back({D3D12_DESCRIPTOR_RANGE_TYPE_CBV, 1, i, 0, D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND});
		vdesc_range.push_back({D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 1, i, 0, D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND});
	}

	root_param.ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
	root_param.ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
	root_param.DescriptorTable.NumDescriptorRanges = 1;
	for (auto & x : vdesc_range) {
		root_param.DescriptorTable.pDescriptorRanges = &x;
		vroot_param.push_back(root_param);
	}

	root_signature_desc.NumParameters = vroot_param.size();
	root_signature_desc.pParameters = vroot_param.data();

	auto hr = D3D12SerializeRootSignature(&root_signature_desc, D3D_ROOT_SIGNATURE_VERSION_1_0, &signature, &perrblob);
	if (hr && perrblob) {
		printf("Failed D3D12SerializeRootSignature:\n%s\n", (char *) perrblob->GetBufferPointer());
		exit(1);
	}

	hr = dev->CreateRootSignature(0, signature->GetBufferPointer(), signature->GetBufferSize(), IID_PPV_ARGS(&rootsig));
	return rootsig;
}

int
main(int argc, char *argv[])
{
	struct frameinfo {
		ID3D12Resource *uav_cbuffer = 0;
		ID3D12Resource *uav_dbuffer = 0;
		ID3D12Resource *cbv_buffer = 0;
		void *data;
		dx12context ctx;
		dx12heap heap_cbv_uav;
	};
	enum {
		Width = 1280,
		Height = 720,
		BufferCount = 2,
	};
	auto hwnd = init_window(argv[0], Width, Height);
	dx12device gpudev;
	std::vector<frameinfo> frames;
	gpudev.init(hwnd, Width, Height, BufferCount);
	auto dev = gpudev.dev;
	for (int i = 0 ; i  < BufferCount; i++) {
		auto swap_chain = gpudev.swap_chain;
		frameinfo fi{};
		fi.ctx.init(dev, swap_chain, i);
		fi.heap_cbv_uav.init(dev, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
		fi.uav_cbuffer = create_res(dev, Width, Height, DXGI_FORMAT_R8G8B8A8_UNORM, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);
		fi.cbv_buffer = create_res(dev, 256, 1, DXGI_FORMAT_UNKNOWN, D3D12_RESOURCE_FLAG_NONE, D3D12_HEAP_TYPE_UPLOAD);
		fi.cbv_buffer->Map(0, NULL, reinterpret_cast<void **>(&fi.data));
		create_cbv(dev, fi.cbv_buffer, fi.heap_cbv_uav.get_hcpu(dev, fi.cbv_buffer));

		create_uav(dev, fi.uav_cbuffer, 0, 0, fi.heap_cbv_uav.get_hcpu(dev, fi.uav_cbuffer));
		frames.push_back(fi);
	}
	ID3D12RootSignature *rootsig = create_rootsig(gpudev.dev, 4);
	ID3D12PipelineState *cpstate = NULL;
	D3D12_COMPUTE_PIPELINE_STATE_DESC ccpstate_desc = {};
	std::vector<uint8_t> cs;
	ccpstate_desc.pRootSignature = rootsig;
	ccpstate_desc.CS = create_shader_from_file(std::string("test.hlsl"), "CSMain", "cs_5_0", cs);
	auto status = dev->CreateComputePipelineState(&ccpstate_desc, IID_PPV_ARGS(&cpstate));

	for (uint64_t frame = 0 ; update_window() ; frame++) {
		auto index = frame % BufferCount;
		auto dev = gpudev.dev;
		auto queue = gpudev.queue;
		auto swap_chain = gpudev.swap_chain;

		auto & fi = frames[index];
		auto & ctx = fi.ctx;
		auto cmdlist = ctx.cmdlist;
		struct info {
			uint32_t width;
			uint32_t height;
			uint32_t reserved;
			uint32_t frame;
		};
		info *pinfo = (info *)fi.data;
		pinfo->width = Width;
		pinfo->height = Height;
		pinfo->frame = frame;
		std::vector<ID3D12DescriptorHeap *> heaps;
		heaps.push_back(fi.heap_cbv_uav.heap);
		std::vector<ID3D12CommandList *> vcmdlists;
		vcmdlists.push_back(cmdlist);

		auto value = ctx.fence->GetCompletedValue();
		if (value != ctx.fence_value) {
			printf("sig : %lld\n", value);
			auto hevent = CreateEventEx(NULL, FALSE, FALSE, EVENT_ALL_ACCESS);
			ctx.fence->SetEventOnCompletion(ctx.fence_value, hevent);
			WaitForSingleObject(hevent, INFINITE);
			CloseHandle(hevent);
		}

		ctx.fence_value = frame;
		ctx.cmdalloc->Reset();
		cmdlist->Reset(ctx.cmdalloc, 0);
		cmdlist->SetDescriptorHeaps(heaps.size(), heaps.data());
		cmdlist->SetComputeRootSignature(rootsig);
		cmdlist->SetComputeRootDescriptorTable(1, fi.heap_cbv_uav.get_hgpu(dev, fi.cbv_buffer));
		cmdlist->SetComputeRootDescriptorTable(2, fi.heap_cbv_uav.get_hgpu(dev, fi.uav_cbuffer));
		cmdlist->SetPipelineState(cpstate);
		cmdlist->Dispatch(Width / 8, Height / 8, 1);
		copy_res_data(dev, cmdlist, ctx.back_buffer, fi.uav_cbuffer);
		cmdlist->Close();
		queue->ExecuteCommandLists(vcmdlists.size(), vcmdlists.data());
		queue->Signal(ctx.fence, frame);
		swap_chain->Present(1, 0);
	}
	return 0;
}

