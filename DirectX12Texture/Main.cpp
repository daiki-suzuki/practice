#include <windows.h>
#include <wrl.h>

#include <d3d12.h>
#pragma comment(lib, "d3d12.lib")
#include <dxgi1_4.h>
#pragma comment(lib, "dxgi.lib")
#include <d3dcompiler.h>
#pragma comment(lib, "d3dcompiler.lib")

#include <DirectXMath.h>
#include <vector>
#include <fstream>

using namespace DirectX;
using Microsoft::WRL::ComPtr;
using namespace std;

LRESULT CALLBACK WindowProc( HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam );

bool Init( HWND hwnd );
bool InitPipeline( HWND hwnd );
bool InitResource();
bool Render();
bool PopulateCommandList();
bool WaitForPreviousFrame();
bool Destroy();

std::vector<UINT8> LoadTexture( const char* fileName );

const UINT FRAME_COUNT = 2;

struct Vertex
{
	XMFLOAT3 possition;
	//XMFLOAT4 color;
	XMFLOAT2 uv;
};

//パイプラインオブジェクト
ComPtr<ID3D12Device> g_device;
ComPtr<ID3D12CommandQueue> g_commandQueue;
ComPtr<IDXGISwapChain3> g_swapChain;
ComPtr<ID3D12DescriptorHeap> g_rtvHeap;
ComPtr<ID3D12Resource> g_renderTarget[FRAME_COUNT];
ComPtr<ID3D12CommandAllocator> g_commandAllocator;
ComPtr<ID3D12GraphicsCommandList> g_commandList;
ComPtr<ID3D12PipelineState> g_pipelineState;
UINT g_rtvDescriptorSize = 0;
ComPtr<ID3D12RootSignature> g_rootSignature;
D3D12_VIEWPORT g_viewport = { 0.0f, 0.0f, 1280.0f, 720.0f };
D3D12_RECT g_scissorRect = { 0, 0, 1280, 720 };
ComPtr<ID3D12DescriptorHeap> g_srvHeap;

//リソース
ComPtr<ID3D12Resource> g_vertexBuffer;
D3D12_VERTEX_BUFFER_VIEW g_vertexBufferView;
ComPtr<ID3D12Resource> g_texture;

//同期オブジェクト
ComPtr<ID3D12Fence> g_fence;
UINT g_frameIndex = 0;
UINT g_fenceValue;
HANDLE g_fenceEvent;

bool g_useWarpDevice = false;
float g_aspectRatio;


int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE, LPSTR, int nCmdShow)
{
	//ウィンドウの初期化-------------------------------
	WNDCLASSEX windowClass = {0};
	windowClass.cbSize = sizeof(windowClass);
	windowClass.style = CS_HREDRAW | CS_VREDRAW;
	windowClass.lpfnWndProc = WindowProc;
	windowClass.hInstance = hInstance;
	windowClass.hCursor = LoadCursor(NULL, IDC_ARROW);
	windowClass.lpszClassName = "DirectX12三角形描画";
	RegisterClassEx(&windowClass);

	RECT windowRect = {0,0,static_cast<LONG>(1280),static_cast<LONG>(720)};
	AdjustWindowRect(&windowRect, WS_OVERLAPPEDWINDOW, FALSE);

	g_aspectRatio = static_cast<float>(1280) / static_cast<float>(720);

	HWND hwnd = CreateWindow( windowClass.lpszClassName,
		windowClass.lpszClassName,
		WS_OVERLAPPEDWINDOW,
		CW_USEDEFAULT,
		CW_USEDEFAULT,
		windowRect.right - windowRect.left,
		windowRect.bottom - windowRect.top,
		nullptr,
		nullptr,
		hInstance,
		nullptr,
		);

	if(!Init(hwnd))
	{
		return -1;
	}

	ShowWindow(hwnd,SW_SHOW);
	UpdateWindow(hwnd);

	MSG msg;
	while(true)
	{
		if(PeekMessage(&msg,nullptr,0,0,PM_REMOVE))
		{
			if( msg.message == WM_QUIT )
			{
				break;
			}

			TranslateMessage(&msg);
			DispatchMessage(&msg);
		}
		else
		{
			if(!Render())
			{
				return -1;
			}
		}
	}

	if(!Destroy())
	{
		return -1;
	}

	return 0;
}

LRESULT CALLBACK WindowProc( HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam )
{
	switch(message)
	{
	case WM_DESTROY:
		//終了メッセージ送信
		PostQuitMessage(0);

		return 0;
	}

	return DefWindowProc(hWnd, message, wParam, lParam);
}

//初期化
bool Init(HWND hwnd )
{
	//DirectX12の初期化

	//パイプラインの初期化
	if(!InitPipeline( hwnd ))
	{
		return false;
	}
	
	//リソースの初期化
	if(!InitResource())
	{
		return false;
	}

	return true;
}

//パイプラインの初期化
bool InitPipeline( HWND hwnd )
{
	UINT digixFactoryFlags = 0;

#if defined(_DEBUG)
	//デバッグレイヤーを有効にする
	{
		ComPtr<ID3D12Debug> debugController;
		if( SUCCEEDED( D3D12GetDebugInterface( IID_PPV_ARGS( &debugController ) ) ) )
		{
			debugController->EnableDebugLayer();

			//デバッグ用のフラグを追加
			digixFactoryFlags |= DXGI_CREATE_FACTORY_DEBUG;
		}
	}
#endif

	//DirectX12がサポートする利用可能なハードウエアアダプタを取得
	ComPtr<IDXGIFactory4> factory;
	if( FAILED( CreateDXGIFactory2( digixFactoryFlags,IID_PPV_ARGS(&factory) ) ) )
	{
		return false;
	}

	//デバイス作成
	if( g_useWarpDevice )
	{
		ComPtr<IDXGIAdapter> warpAdapter;
		if( FAILED(factory->EnumWarpAdapter(IID_PPV_ARGS(&warpAdapter))) )
		{
			return false;
		}
		if( FAILED(D3D12CreateDevice(
			warpAdapter.Get(),
			D3D_FEATURE_LEVEL_11_0,
			IID_PPV_ARGS(&g_device))))
		{
			return false;
		}
	}
	else
	{
		ComPtr<IDXGIAdapter1> hardwareAdapter;
		ComPtr<IDXGIAdapter1> adapter;
		IDXGIAdapter1** ppAdapter = &hardwareAdapter;
		*ppAdapter = nullptr;
		IDXGIFactory2* pFactory = factory.Get();
		for(UINT adapterIndex = 0; DXGI_ERROR_NOT_FOUND != pFactory->EnumAdapters1( adapterIndex, &adapter ); adapterIndex++)
		{
			DXGI_ADAPTER_DESC1 desc;
			adapter->GetDesc1(&desc);

			if( desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE )
			{
				continue;
			}

			if( SUCCEEDED( D3D12CreateDevice( adapter.Get(), D3D_FEATURE_LEVEL_11_0, __uuidof(ID3D12Device), nullptr ) ) )
			{
				break;
			}

		}
		*ppAdapter = adapter.Detach();

		if( FAILED(D3D12CreateDevice(
			hardwareAdapter.Get(),
			D3D_FEATURE_LEVEL_11_0,
			IID_PPV_ARGS(&g_device))))
		{
			return false;
		}
	}

	//コマンドキュー作成
	D3D12_COMMAND_QUEUE_DESC queueDesc = {};
	queueDesc.Flags = D3D12_COMMAND_QUEUE_FLAG_NONE;
	queueDesc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;

	if( FAILED( g_device->CreateCommandQueue( &queueDesc, IID_PPV_ARGS(&g_commandQueue) ) ) )
	{
		return false;
	}


	//スワップチェインを作成
	DXGI_SWAP_CHAIN_DESC1 swapChainDesc = {};
	swapChainDesc.BufferCount = 2;
	swapChainDesc.Width = 1280;
	swapChainDesc.Height = 720;
	swapChainDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
	swapChainDesc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
	swapChainDesc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
	swapChainDesc.SampleDesc.Count = 1;

	ComPtr<IDXGISwapChain1> swapChain;
	if( FAILED( factory->CreateSwapChainForHwnd(
		g_commandQueue.Get(),
		hwnd,
		&swapChainDesc,
		nullptr,
		nullptr,
		&swapChain ) ) )
	{
		return false;
	}

	//フルスクリーンサポートなし
	if( FAILED( factory->MakeWindowAssociation( hwnd, DXGI_MWA_NO_ALT_ENTER ) ) )
	{
		return false;
	}

	if( FAILED( swapChain.As(&g_swapChain) ) )
	{
		return false;
	}
	g_frameIndex = g_swapChain->GetCurrentBackBufferIndex();

	
	
	//記述子ヒープ作成
	{
		//レンダーターゲットビュー用の記述子ヒープ作成
		D3D12_DESCRIPTOR_HEAP_DESC rtvHeapDesc = {};
		rtvHeapDesc.NumDescriptors = 2;
		rtvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
		rtvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
		if( FAILED( g_device->CreateDescriptorHeap(&rtvHeapDesc, IID_PPV_ARGS(&g_rtvHeap)) ) )
		{
			return false;
		}

		//シェーダーリソースビュー用の記述子ヒープ作成
		D3D12_DESCRIPTOR_HEAP_DESC srvHeapDesc = {};
		srvHeapDesc.NumDescriptors = 1;
		srvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
		srvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
		if( FAILED( g_device->CreateDescriptorHeap(&srvHeapDesc, IID_PPV_ARGS(&g_srvHeap)) ) )
		{
			return false;
		}

		g_rtvDescriptorSize = g_device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
	}

	//フレームリソースの作成
	{
		D3D12_CPU_DESCRIPTOR_HANDLE	rtvHandle = {};
		rtvHandle.ptr = g_rtvHeap->GetCPUDescriptorHandleForHeapStart().ptr;

		//各フレームのレンダーターゲットビューを作成
		for(UINT n = 0;n < FRAME_COUNT;n++)
		{
			if( FAILED( g_swapChain->GetBuffer( n, IID_PPV_ARGS( &g_renderTarget[n] ) ) ) )
			{
				return false;
			}
			g_device->CreateRenderTargetView( g_renderTarget[n].Get(), nullptr, rtvHandle );
			rtvHandle.ptr += g_rtvDescriptorSize;

		}
	}
	
	//コマンドアロケーター作成
	if( FAILED( g_device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&g_commandAllocator)) ) )
	{
		return false;
	}

	return true;
}

//リソースの初期化
bool InitResource()
{
	//ルートシグネチャを作成
	{
		D3D12_FEATURE_DATA_ROOT_SIGNATURE featureData = {};

		//最大のバージョンを指定(指定より高いバージョンにならない)
		featureData.HighestVersion = D3D_ROOT_SIGNATURE_VERSION_1_1;

		if( FAILED( g_device->CheckFeatureSupport( D3D12_FEATURE_ROOT_SIGNATURE, &featureData, sizeof(featureData) ) ) )
		{
			featureData.HighestVersion = D3D_ROOT_SIGNATURE_VERSION_1_0;
		}

		D3D12_DESCRIPTOR_RANGE1 ranges[1];
		ranges[0].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
		ranges[0].NumDescriptors = 1;
		ranges[0].BaseShaderRegister = 0;
		ranges[0].RegisterSpace = 0;
		ranges[0].Flags = D3D12_DESCRIPTOR_RANGE_FLAG_DATA_STATIC;
		ranges[0].OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

		D3D12_ROOT_PARAMETER1 rootParameters[1];
		rootParameters[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
		rootParameters[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
		rootParameters[0].DescriptorTable.NumDescriptorRanges = 1;
		rootParameters[0].DescriptorTable.pDescriptorRanges = &ranges[0];

		D3D12_STATIC_SAMPLER_DESC sampler = {};
		sampler.Filter = D3D12_FILTER_MIN_MAG_MIP_POINT;
		sampler.AddressU = D3D12_TEXTURE_ADDRESS_MODE_BORDER;
		sampler.AddressV = D3D12_TEXTURE_ADDRESS_MODE_BORDER;
		sampler.AddressW = D3D12_TEXTURE_ADDRESS_MODE_BORDER;
		sampler.MipLODBias = 0;
		sampler.MaxAnisotropy = 0;
		sampler.ComparisonFunc = D3D12_COMPARISON_FUNC_NEVER;
		sampler.BorderColor = D3D12_STATIC_BORDER_COLOR_OPAQUE_BLACK;
		sampler.MinLOD = 0.0f;
		sampler.MaxLOD = D3D12_FLOAT32_MAX;
		sampler.ShaderRegister = 0;
		sampler.RegisterSpace = 0;
		sampler.ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

		
		//D3D12_ROOT_SIGNATURE_DESC rootSignetureDesc = {};
		/*rootSignetureDesc.NumParameters = 0;
		rootSignetureDesc.pParameters = nullptr;
		rootSignetureDesc.NumStaticSamplers = 0;
		rootSignetureDesc.pStaticSamplers = nullptr;
		rootSignetureDesc.Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;*/

		D3D12_VERSIONED_ROOT_SIGNATURE_DESC rootSignetureDesc = {};
		rootSignetureDesc.Version = D3D_ROOT_SIGNATURE_VERSION_1_1;
		rootSignetureDesc.Desc_1_1.NumParameters = _countof(rootParameters);
		rootSignetureDesc.Desc_1_1.pParameters = rootParameters;
		rootSignetureDesc.Desc_1_1.NumStaticSamplers = 1;
		rootSignetureDesc.Desc_1_1.pStaticSamplers = &sampler;
		rootSignetureDesc.Desc_1_1.Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;


		ComPtr<ID3DBlob> signature;
		ComPtr<ID3DBlob> error;
		/*if( FAILED( D3D12SerializeRootSignature( &rootSignetureDesc, D3D_ROOT_SIGNATURE_VERSION_1, &signature, &error ) ) )
		{
			return false;
		}*/
		if( featureData.HighestVersion == D3D_ROOT_SIGNATURE_VERSION_1_1 )
		{
			if( FAILED( D3D12SerializeVersionedRootSignature( &rootSignetureDesc, &signature, &error ) ) )
			{
				return false;
			}
		}
		else
		{
			return false;
		}
		if( FAILED( g_device->CreateRootSignature( 0, signature->GetBufferPointer(), signature->GetBufferSize(), IID_PPV_ARGS(&g_rootSignature) ) ) )
		{
			return false;
		}
	}

	//シェーダーをコンパイル
	{
		ComPtr<ID3DBlob> vertexShader;
		ComPtr<ID3DBlob> pixelShader;

#if defined(_DEBUG)
		UINT compileFlag = D3DCOMPILE_DEBUG | D3DCOMPILE_SKIP_OPTIMIZATION;;
#else
		UINT compileFlag = 0;
#endif
		if( FAILED(D3DCompileFromFile(L"shader.hlsl", nullptr, nullptr, "VSMain", "vs_5_0", compileFlag, 0, &vertexShader, nullptr) ) )
		{
			return false;
		}
		if( FAILED(D3DCompileFromFile(L"shader.hlsl", nullptr, nullptr, "PSMain", "ps_5_0", compileFlag, 0, &pixelShader, nullptr) ) )
		{
			return false;
		}

		//頂点入力レイヤーを定義
		D3D12_INPUT_ELEMENT_DESC inputElementDescs[] =
		{
			{"POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
			//{"COLOR", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 12, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
			{"TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 12, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
		};

		//グラフィックスパイプラインの状態オブジェクトを作成
		D3D12_GRAPHICS_PIPELINE_STATE_DESC psoDesc = {};
		psoDesc.InputLayout = { inputElementDescs, _countof(inputElementDescs) };
		psoDesc.pRootSignature = g_rootSignature.Get();

		psoDesc.VS.pShaderBytecode = vertexShader->GetBufferPointer();
		psoDesc.VS.BytecodeLength = vertexShader->GetBufferSize();
		
		psoDesc.PS.pShaderBytecode = pixelShader->GetBufferPointer();
		psoDesc.PS.BytecodeLength = pixelShader->GetBufferSize();
		
		psoDesc.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID;
		psoDesc.RasterizerState.CullMode = D3D12_CULL_MODE_BACK;
		psoDesc.RasterizerState.FrontCounterClockwise = FALSE;
		psoDesc.RasterizerState.DepthBias = D3D12_DEFAULT_DEPTH_BIAS;
		psoDesc.RasterizerState.DepthBiasClamp = D3D12_DEFAULT_DEPTH_BIAS_CLAMP;
		psoDesc.RasterizerState.SlopeScaledDepthBias = D3D12_DEFAULT_SLOPE_SCALED_DEPTH_BIAS;
		psoDesc.RasterizerState.DepthClipEnable = TRUE;
		psoDesc.RasterizerState.MultisampleEnable = FALSE;
		psoDesc.RasterizerState.AntialiasedLineEnable = FALSE;
		psoDesc.RasterizerState.ForcedSampleCount = 0;
		psoDesc.RasterizerState.ConservativeRaster = D3D12_CONSERVATIVE_RASTERIZATION_MODE_OFF;
		
		psoDesc.BlendState.AlphaToCoverageEnable = FALSE;
		psoDesc.BlendState.IndependentBlendEnable = FALSE;
		const D3D12_RENDER_TARGET_BLEND_DESC defaultRenderTargetBlendDesc =
		{
			FALSE,FALSE,
			D3D12_BLEND_ONE, D3D12_BLEND_ZERO, D3D12_BLEND_OP_ADD,
			D3D12_BLEND_ONE, D3D12_BLEND_ZERO, D3D12_BLEND_OP_ADD,
			D3D12_LOGIC_OP_NOOP,
			D3D12_COLOR_WRITE_ENABLE_ALL,
		};
		for (UINT i = 0; i < D3D12_SIMULTANEOUS_RENDER_TARGET_COUNT; ++i)
			psoDesc.BlendState.RenderTarget[ i ] = defaultRenderTargetBlendDesc;

		psoDesc.DepthStencilState.DepthEnable = FALSE;
		psoDesc.DepthStencilState.StencilEnable = FALSE;
		psoDesc.SampleMask = UINT_MAX;
		psoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
		psoDesc.NumRenderTargets = 1;
		psoDesc.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM;
		psoDesc.SampleDesc.Count = 1;
		if( FAILED( g_device->CreateGraphicsPipelineState( &psoDesc, IID_PPV_ARGS(&g_pipelineState) ) ) )
		{
			return false;
		}
	}

	//コマンドリスト作成
	if( FAILED( g_device->CreateCommandList( 0, D3D12_COMMAND_LIST_TYPE_DIRECT, g_commandAllocator.Get(), nullptr, IID_PPV_ARGS( &g_commandList ) ) ) )
	{
		return false;
	}

	//コマンドリストを閉じる
	if( FAILED( g_commandList->Close() ) )
	{
		return false;
	}

	//頂点バッファを作成
	{
		//三角形のジオメトリを定義
		Vertex triangleVertices[] =
		{
			{ {0.0f, 0.25f * g_aspectRatio, 0.0f}, { 0.5f, 0.0f } },
			{ {0.25f, -0.25f * g_aspectRatio, 0.0f}, { 1.0f, 1.0f } },
			{ {-0.25f, -0.25f * g_aspectRatio, 0.0f}, { 0.0f, 1.0f } },
		};

		const UINT vertexBufferSize = sizeof(triangleVertices);

		{
			D3D12_HEAP_PROPERTIES heapProperties = {};
			heapProperties.Type = D3D12_HEAP_TYPE_UPLOAD;
			heapProperties.CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_UNKNOWN;
			heapProperties.MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN;
			heapProperties.CreationNodeMask = 1;
			heapProperties.VisibleNodeMask = 1;

			D3D12_RESOURCE_DESC resourceDesc = {};
			resourceDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
			resourceDesc.Alignment = 0;
			resourceDesc.Width = vertexBufferSize;
			resourceDesc.Height = 1;
			resourceDesc.DepthOrArraySize = 1;
			resourceDesc.MipLevels = 1;
			resourceDesc.Format = DXGI_FORMAT_UNKNOWN;
			resourceDesc.SampleDesc.Count = 1;
			resourceDesc.SampleDesc.Quality = 0;
			resourceDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
			resourceDesc.Flags = D3D12_RESOURCE_FLAG_NONE;

			if( FAILED( g_device->CreateCommittedResource(&heapProperties,D3D12_HEAP_FLAG_NONE,&resourceDesc,D3D12_RESOURCE_STATE_GENERIC_READ,nullptr,IID_PPV_ARGS(&g_vertexBuffer)) ) )
			{
				return false;
			}
		}

		//三角形のデータを頂点バッファにコピーする
		UINT8* pVertexDataBegin;
		D3D12_RANGE readRange = { 0, 0 };
		if( FAILED( g_vertexBuffer->Map( 0, &readRange, reinterpret_cast<void**>(&pVertexDataBegin) ) ))
		{
			return false;
		}
		memcpy(pVertexDataBegin,triangleVertices, sizeof(triangleVertices) );
		g_vertexBuffer->Unmap( 0, nullptr );

		//頂点バッファビューの初期化
		g_vertexBufferView.BufferLocation = g_vertexBuffer->GetGPUVirtualAddress();
		g_vertexBufferView.StrideInBytes = sizeof(Vertex);
		g_vertexBufferView.SizeInBytes = vertexBufferSize;

	}

	//テクスチャの作成
	{
		std::vector<UINT8> texture = LoadTexture( "test.bmp" );

		D3D12_RESOURCE_DESC textureDesc = {};
		textureDesc.MipLevels = 1;
		textureDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
		textureDesc.Width = 256;
		textureDesc.Height = 256;
		textureDesc.Flags = D3D12_RESOURCE_FLAG_NONE;
		textureDesc.DepthOrArraySize = 1;
		textureDesc.SampleDesc.Count = 1;
		textureDesc.SampleDesc.Quality = 1;
		textureDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;

		D3D12_HEAP_PROPERTIES heapProperties = {};
		heapProperties.Type = D3D12_HEAP_TYPE_CUSTOM;
		heapProperties.CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_WRITE_BACK;
		heapProperties.MemoryPoolPreference = D3D12_MEMORY_POOL_L0;
		heapProperties.CreationNodeMask = 1;
		heapProperties.VisibleNodeMask = 1;

		if( FAILED( g_device->CreateCommittedResource(&heapProperties,D3D12_HEAP_FLAG_NONE, &textureDesc, D3D12_RESOURCE_STATE_COPY_DEST, nullptr, IID_PPV_ARGS(&g_texture)) ) )
		{
			return false;
		}

		//テクスチャ用のシェーダーリソースビューを作成
		D3D12_CPU_DESCRIPTOR_HANDLE cpuDescriptorHandle = {};
		D3D12_SHADER_RESOURCE_VIEW_DESC shaderResourceViewDesc = {};
		shaderResourceViewDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
		shaderResourceViewDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
		shaderResourceViewDesc.Texture2D.MipLevels = 1;
		shaderResourceViewDesc.Texture2D.MostDetailedMip = 0;
		shaderResourceViewDesc.Texture2D.PlaneSlice = 0;
		shaderResourceViewDesc.Texture2D.ResourceMinLODClamp = 0.0f;
		shaderResourceViewDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
		g_device->CreateShaderResourceView(g_texture.Get(), &shaderResourceViewDesc, g_srvHeap->GetCPUDescriptorHandleForHeapStart());

		D3D12_BOX box = {0,0,0,(UINT)256,(UINT)256,1};
		if(FAILED(g_texture->WriteToSubresource(0,&box,&texture[0],4*256,4*256*256)))
		{
			return false;
		}
	}

	//同期オブジェクトを作成してリソースがGPUがアップロードされるまで待つ
	{
		//フェンス作成
		if( FAILED( g_device->CreateFence( 0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&g_fence) ) ) )
		{
			return false;
		}
		g_fenceValue = 1;

		//イベントハンドル作成
		g_fenceEvent = CreateEvent( nullptr, FALSE, FALSE, nullptr );
		if(g_fenceEvent == nullptr)
		{
			if( FAILED( HRESULT_FROM_WIN32( GetLastError() ) ) )
			{
				return false;
			}
		}

		//前のフレームを待つ
		if(!WaitForPreviousFrame())
		{
			return false;
		}
	}

	return true;
}

//描画
bool Render()
{
	//コマンドリストの内容を用意する
	if(!PopulateCommandList())
	{
		return false;
	}

	//コマンドリストを実行
	ID3D12CommandList* ppCommandList[] = {g_commandList.Get()};
	g_commandQueue->ExecuteCommandLists(_countof(ppCommandList), ppCommandList);

	//バックバッファを表示
	if( FAILED( g_swapChain->Present(1,0) ) )
	{
		return -1;
	}

	//前のフレームを待つ
	if(!WaitForPreviousFrame())
	{
		return false;
	}

	return true;
}

bool PopulateCommandList()
{
	//コマンドリストのアロケーターをリセット
	if(FAILED( g_commandAllocator->Reset() ))
	{
		return false;
	}

	//コマンドリストをリセット
	if( FAILED( g_commandList->Reset( g_commandAllocator.Get(), g_pipelineState.Get() ) ) )
	{
		return false;
	}

	//必要な情報を設定
	g_commandList->SetGraphicsRootSignature(g_rootSignature.Get());

	ID3D12DescriptorHeap* ppHeap[] = {g_srvHeap.Get()};
	g_commandList->SetDescriptorHeaps( _countof(ppHeap), ppHeap );

	g_commandList->SetGraphicsRootDescriptorTable(0,g_srvHeap->GetGPUDescriptorHandleForHeapStart());

	g_commandList->RSSetViewports(1, &g_viewport);
	g_commandList->RSSetScissorRects( 1, &g_scissorRect );

	//リソースバリアを設定してバックバッファをレンダーターゲットとして指定
	{
		D3D12_RESOURCE_BARRIER resourceBarrier = {};
		resourceBarrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
		resourceBarrier.Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
		resourceBarrier.Transition.pResource = g_renderTarget[g_frameIndex].Get();
		resourceBarrier.Transition.StateBefore = D3D12_RESOURCE_STATE_PRESENT;
		resourceBarrier.Transition.StateAfter = D3D12_RESOURCE_STATE_RENDER_TARGET;
		resourceBarrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
		g_commandList->ResourceBarrier( 1, &resourceBarrier );
	}

	D3D12_CPU_DESCRIPTOR_HANDLE	rtvHandle = {};
	rtvHandle.ptr = g_rtvHeap->GetCPUDescriptorHandleForHeapStart().ptr+g_frameIndex*g_rtvDescriptorSize;
	g_commandList->OMSetRenderTargets(1, &rtvHandle, FALSE, nullptr);

	//バックバッファ描画
	const float clearColor[] = { 0.0f, 0.2f, 0.4f, 1.0f };
	g_commandList->ClearRenderTargetView( rtvHandle, clearColor, 0, nullptr );
	g_commandList->IASetPrimitiveTopology( D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
	g_commandList->IASetVertexBuffers(0, 1, &g_vertexBufferView);
	g_commandList->DrawInstanced(3, 1, 0, 0);

	//バックバッファを表示
	{
		D3D12_RESOURCE_BARRIER resourceBarrier = {};
		resourceBarrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
		resourceBarrier.Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
		resourceBarrier.Transition.pResource = g_renderTarget[g_frameIndex].Get();
		resourceBarrier.Transition.StateBefore = D3D12_RESOURCE_STATE_RENDER_TARGET;
		resourceBarrier.Transition.StateAfter = D3D12_RESOURCE_STATE_PRESENT;
		resourceBarrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
		g_commandList->ResourceBarrier( 1, &resourceBarrier );
	}

	//コマンドリストを閉じる
	if( FAILED( g_commandList->Close() ) )
	{
		return false;
	}

	return true;
}

bool WaitForPreviousFrame()
{
	//簡単に実装するために続行する前にフレームが終わるまで待つ
	const UINT64 fence = g_fenceValue;
	if( FAILED( g_commandQueue->Signal( g_fence.Get(), fence ) ) )
	{
		return false;
	}
	g_fenceValue++;
	if( g_fence->GetCompletedValue() < fence )
	{
		if( FAILED( g_fence->SetEventOnCompletion( fence, g_fenceEvent ) ))
		{
			return false;
		}
		WaitForSingleObject(g_fenceEvent,INFINITE);
	}
	g_frameIndex = g_swapChain->GetCurrentBackBufferIndex();

	return true;
}

bool Destroy()
{
	//前のフレームを待つ
	if(!WaitForPreviousFrame())
	{
		return false;
	}

	//イベントハンドルを閉じる
	CloseHandle(g_fenceEvent);
	
	return true;
}

std::vector<UINT8> LoadTexture( const char* fileName )
{
	ifstream ifile( fileName, ios::binary );

	vector<UINT8> data(256*256 * 4);

	if( !ifile.is_open() )
	{
		return data;
	}
#pragma pack(1)
	struct BitmapHeader
	{
		unsigned char type[2];
		unsigned int size;
		unsigned short reserved[2];
		unsigned int offset;
	};
#pragma pack()

	struct BitmapInfoHeader
	{
		unsigned int size;
		int width;
		int height;
		unsigned short planes;
		unsigned short bitCount;
		unsigned int compression;
		unsigned int sizeImage;
		int XPixPerMeter;
		int YPixPerMeter;
		unsigned int ClrUsed;
		unsigned int CirImportant;
	};
	struct BitmapColor24
	{
		unsigned char b;
		unsigned char g;
		unsigned char r;
	};

	struct BitmapColor32
	{
		unsigned char b;
		unsigned char g;
		unsigned char r;
		unsigned char a;
	};

	BitmapHeader header;
	BitmapInfoHeader infoHeader;

	ifile.read(reinterpret_cast<char*>(&header),sizeof(header));
	ifile.read(reinterpret_cast<char*>(&infoHeader),sizeof(infoHeader));
	
	int colorSize = infoHeader.width * infoHeader.height;
	if(infoHeader.bitCount == 24)
	{
		BitmapColor24* color = new BitmapColor24[colorSize];
		ifile.read(reinterpret_cast<char*>(color),sizeof(BitmapColor24) * colorSize);
		BitmapColor32* color32 = new BitmapColor32[colorSize];
		for(int i = 0;i < infoHeader.height / 2;i++)
		{
			for(int k = 0;k < infoHeader.width;k++)
			{
				int index1 = k + i * infoHeader.width;
				int index2 = k + (infoHeader.height-1-i) * infoHeader.width;
				BitmapColor24 tmp = color[index1];
				color[index1] = color[index2];
				color[index2] = tmp;
			}
		}
		for(int i = 0;i < colorSize;i++)
		{
			color32[i].r = color[i].b;
			color32[i].g = color[i].g;
			color32[i].b = color[i].r;
			color32[i].a = 255;
		}

		memcpy(&data[0],color32,sizeof(BitmapColor32) * colorSize);

		delete[] color;
		delete[] color32;
	}
	else if(infoHeader.bitCount == 32)
	{
		ifile.read(reinterpret_cast<char*>(&data[0]),sizeof(BitmapColor24) * colorSize);
	}

	ifile.close();

	return data;
}

