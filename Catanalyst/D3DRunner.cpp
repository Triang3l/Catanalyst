#include "Catanalyst.h"

#include <Windows.h>

#include <d3d11_1.h>
#include <d3dcompiler.h>
#include <dxgi.h>
#include <wrl/client.h>

#include <cstdio>
#include <cstddef>
#include <cstdlib>
#include <cstdint>
#include <memory>
#include <utility>

using Microsoft::WRL::ComPtr;

static Microsoft::WRL::ComPtr<ID3DBlob> D3DR_CompileShader(char const * const source,
                                                           size_t const source_bytes,
                                                           char const * const profile) {
   Microsoft::WRL::ComPtr<ID3DBlob> bytecode, error_messages;
   if (FAILED(D3DCompile(source, source_bytes, nullptr, nullptr, nullptr, "main", profile, 0, 0,
                         &bytecode, &error_messages))) {
      std::fprintf(stderr, "Failed to compile a %s shader.\n", profile);
      if (error_messages) {
         std::fwrite(error_messages->GetBufferPointer(), error_messages->GetBufferSize(), 1,
                     stderr);
         std::fputc('\n', stderr);
      }
      return nullptr;
   }
   return std::move(bytecode);
}

#define D3DR_SCRATCH_CONSTANTS \
   "   uint temp_index_0;\r\n" \
   "   uint temp_index_1;\r\n" \
   "   uint temp_index_2;\r\n" \
   "   uint temp_value_0;\r\n" \
   "   uint temp_value_1;\r\n"
#define D3DR_SCRATCH_OPERATIONS \
   "   uint temp_array[256];\r\n" \
   "   temp_array[temp_index_0] = temp_value_0;\r\n" \
   "   temp_array[temp_index_0 * 2u] = temp_value_0;\r\n" \
   "   temp_array[temp_index_1] = temp_value_1;\r\n" \
   "   temp_array[temp_index_1 + 1u] = temp_value_1;\r\n"

static bool D3DR_RenderTest(ID3D11Device * const device, ID3D11DeviceContext * const context) {
   context->ClearState();

   char const vertex_shader_source[] =
      "cbuffer cb_constants : register(b1) {\r\n"
      "   float vertex_color_scale;\r\n"
      D3DR_SCRATCH_CONSTANTS
      "}\r\n"
      "void main(\r\n"
      "   float4 in_position : POSITION,\r\n"
      "   float2 in_color : COLOR,\r\n"
      "   out float2 out_color : COLOR,\r\n"
      "   out float4 out_position : SV_Position) {\r\n"
      D3DR_SCRATCH_OPERATIONS
      "   out_color = vertex_color_scale * in_color + float(temp_array[temp_index_2]);\r\n"
      "   out_position = in_position;\r\n"
      "}\r\n";
   Microsoft::WRL::ComPtr<ID3DBlob> vertex_shader_bytecode =
      D3DR_CompileShader(vertex_shader_source, sizeof(vertex_shader_source) - 1, "vs_5_0");
   if (!vertex_shader_bytecode) {
      return false;
   }
   Microsoft::WRL::ComPtr<ID3D11VertexShader> vertex_shader;
   if (FAILED(device->CreateVertexShader(vertex_shader_bytecode->GetBufferPointer(),
                                         vertex_shader_bytecode->GetBufferSize(), nullptr,
                                         &vertex_shader))) {
      std::fputs("Failed to create the vertex shader.\n", stderr);
      return false;
   }

   char const pixel_shader_with_rat_source[] =
      "cbuffer cb_constants : register(b2) {\r\n"
      "   float vertex_color_scale;\r\n"
      "   float pixel_color_scale;\r\n"
      "}\r\n"
      "RWTexture2D<float> rat : register(u2);\r\n"
      "void main(\r\n"
      "   float2 in_color : COLOR,\r\n"
      "   float4 in_position : SV_Position,\r\n"
      "   out float4 out_target0 : SV_Target0,\r\n"
      "   out float4 out_target1 : SV_Target1) {\r\n"
      "   out_target0 = float4(pixel_color_scale * in_color.r, 0.0, 0.0, 1.0);\r\n"
      "   const float old_rat_value = rat[uint2(in_position.xy)];\r\n"
      "   rat[uint2(in_position.xy)] = old_rat_value + in_color.r + in_color.g;\r\n"
      "   out_target1 = float4(0.0, pixel_color_scale * in_color.g + old_rat_value, 0.0, 1.0);\r\n"
      "}\r\n";
   Microsoft::WRL::ComPtr<ID3DBlob> pixel_shader_with_rat_bytecode = D3DR_CompileShader(
      pixel_shader_with_rat_source, sizeof(pixel_shader_with_rat_source) - 1, "ps_5_0");
   if (!pixel_shader_with_rat_bytecode) {
      return false;
   }
   Microsoft::WRL::ComPtr<ID3D11PixelShader> pixel_shader_with_rat;
   if (FAILED(device->CreatePixelShader(pixel_shader_with_rat_bytecode->GetBufferPointer(),
                                        pixel_shader_with_rat_bytecode->GetBufferSize(), nullptr,
                                        &pixel_shader_with_rat))) {
      std::fputs("Failed to create the pixel shader with a RAT.\n", stderr);
      return false;
   }

   D3D11_INPUT_ELEMENT_DESC input_elements[2] = {};
   input_elements[0].SemanticName = "POSITION";
   input_elements[0].Format = DXGI_FORMAT_R32G32B32A32_FLOAT;
   input_elements[1].SemanticName = "COLOR";
   input_elements[1].Format = DXGI_FORMAT_R32G32_FLOAT;
   input_elements[1].InputSlot = 1;
   Microsoft::WRL::ComPtr<ID3D11InputLayout> input_layout;
   if (FAILED(device->CreateInputLayout(input_elements,
                                        sizeof(input_elements) / sizeof(input_elements[0]),
                                        vertex_shader_bytecode->GetBufferPointer(),
                                        vertex_shader_bytecode->GetBufferSize(), &input_layout))) {
      std::fputs("Failed to create the input layout.\n", stderr);
      return false;
   }

   float const vertex_buffer_data[] = {
      -1.0f, -1.0f, 0.0f, 1.0f,
      0.0f, 0.0f,

      1.0f, -1.0f, 0.0f, 1.0f,
      1.0f, 0.0f,

      -1.0f, 1.0f, 0.0f, 1.0f,
      0.0f, 1.0f,

      1.0f, 1.0f, 0.0f, 1.0f,
      1.0f, 1.0f,
   };

   D3D11_SUBRESOURCE_DATA vertex_buffer_initial_data = {};
   vertex_buffer_initial_data.pSysMem = vertex_buffer_data;

   D3D11_BUFFER_DESC vertex_buffer_desc = {};
   vertex_buffer_desc.ByteWidth = sizeof(vertex_buffer_data);
   vertex_buffer_desc.Usage = D3D11_USAGE_IMMUTABLE;
   vertex_buffer_desc.BindFlags = D3D11_BIND_VERTEX_BUFFER;

   Microsoft::WRL::ComPtr<ID3D11Buffer> vertex_buffer;
   if (FAILED(device->CreateBuffer(&vertex_buffer_desc, &vertex_buffer_initial_data,
                                   &vertex_buffer))) {
      std::fputs("Failed to create the vertex buffer.\n", stderr);
      return false;
   }

   uint16_t const index_buffer_data[] = {
      // Skip two to see the offset in the patch locations.
      0, 0,
      0, 1, 2, 3,
   };

   D3D11_SUBRESOURCE_DATA index_buffer_initial_data = {};
   index_buffer_initial_data.pSysMem = index_buffer_data;

   D3D11_BUFFER_DESC index_buffer_desc = {};
   index_buffer_desc.ByteWidth = sizeof(index_buffer_data);
   index_buffer_desc.Usage = D3D11_USAGE_IMMUTABLE;
   index_buffer_desc.BindFlags = D3D11_BIND_INDEX_BUFFER;

   Microsoft::WRL::ComPtr<ID3D11Buffer> index_buffer;
   if (FAILED(device->CreateBuffer(&index_buffer_desc, &index_buffer_initial_data,
                                   &index_buffer))) {
      std::fputs("Failed to create the index buffer.\n", stderr);
      return false;
   }

   float constant_buffer_data[0x40 * 3] = {};
   constant_buffer_data[0x40 * 1] = 0.5f;
   constant_buffer_data[0x40 * 2 + 1] = 2.0f;

   D3D11_SUBRESOURCE_DATA constant_buffer_initial_data = {};
   constant_buffer_initial_data.pSysMem = constant_buffer_data;

   D3D11_BUFFER_DESC constant_buffer_desc = {};
   constant_buffer_desc.ByteWidth = sizeof(constant_buffer_data);
   constant_buffer_desc.Usage = D3D11_USAGE_IMMUTABLE;
   constant_buffer_desc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;

   Microsoft::WRL::ComPtr<ID3D11Buffer> constant_buffer;
   if (FAILED(device->CreateBuffer(&constant_buffer_desc, &constant_buffer_initial_data,
                                   &constant_buffer))) {
      std::fputs("Failed to create the constant buffer.\n", stderr);
      return false;
   }

   uint32_t const rt_width = 1280;
   uint32_t const rt_height = 720;

   D3D11_TEXTURE2D_DESC rt_texture_desc = {};
   rt_texture_desc.Width = rt_width;
   rt_texture_desc.Height = rt_height;
   rt_texture_desc.MipLevels = 1;
   rt_texture_desc.ArraySize = 1;
   rt_texture_desc.Format = DXGI_FORMAT_D32_FLOAT_S8X24_UINT;
   rt_texture_desc.SampleDesc.Count = 1;
   rt_texture_desc.Usage = D3D11_USAGE_DEFAULT;
   rt_texture_desc.BindFlags = D3D11_BIND_DEPTH_STENCIL;
   Microsoft::WRL::ComPtr<ID3D11Texture2D> rt_texture_ds;
   if (FAILED(device->CreateTexture2D(&rt_texture_desc, nullptr, &rt_texture_ds))) {
      std::fputs("Failed to create the depth/stencil texture.\n", stderr);
      return false;
   }
   rt_texture_desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
   rt_texture_desc.BindFlags = D3D11_BIND_RENDER_TARGET;
   Microsoft::WRL::ComPtr<ID3D11Texture2D> rt_texture_mrt0;
   if (FAILED(device->CreateTexture2D(&rt_texture_desc, nullptr, &rt_texture_mrt0))) {
      std::fputs("Failed to create the render target 0 texture.\n", stderr);
      return false;
   }
   Microsoft::WRL::ComPtr<ID3D11Texture2D> rt_texture_mrt1;
   if (FAILED(device->CreateTexture2D(&rt_texture_desc, nullptr, &rt_texture_mrt1))) {
      std::fputs("Failed to create the render target 1 texture.\n", stderr);
      return false;
   }
   rt_texture_desc.Format = DXGI_FORMAT_R32_FLOAT;
   rt_texture_desc.BindFlags = D3D11_BIND_UNORDERED_ACCESS;
   Microsoft::WRL::ComPtr<ID3D11Texture2D> rt_texture_rat;
   if (FAILED(device->CreateTexture2D(&rt_texture_desc, nullptr, &rt_texture_rat))) {
      std::fputs("Failed to create the RAT texture.\n", stderr);
      return false;
   }

   D3D11_DEPTH_STENCIL_VIEW_DESC rt_dsv_desc;
   rt_dsv_desc.Format = DXGI_FORMAT_D32_FLOAT_S8X24_UINT;
   rt_dsv_desc.ViewDimension = D3D11_DSV_DIMENSION_TEXTURE2D;
   rt_dsv_desc.Flags = 0;
   rt_dsv_desc.Texture2D.MipSlice = 0;
   Microsoft::WRL::ComPtr<ID3D11DepthStencilView> rt_dsv;
   if (FAILED(device->CreateDepthStencilView(rt_texture_ds.Get(), &rt_dsv_desc, &rt_dsv))) {
      std::fputs("Failed to create the depth/stencil view.\n", stderr);
      return false;
   }
   D3D11_RENDER_TARGET_VIEW_DESC rt_rtv_desc;
   rt_rtv_desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
   rt_rtv_desc.ViewDimension = D3D11_RTV_DIMENSION_TEXTURE2D;
   rt_rtv_desc.Texture2D.MipSlice = 0;
   Microsoft::WRL::ComPtr<ID3D11RenderTargetView> rt_rtv_mrt0;
   if (FAILED(device->CreateRenderTargetView(rt_texture_mrt0.Get(), &rt_rtv_desc, &rt_rtv_mrt0))) {
      std::fputs("Failed to create the render target 0 RTV.\n", stderr);
      return false;
   }
   Microsoft::WRL::ComPtr<ID3D11RenderTargetView> rt_rtv_mrt1;
   if (FAILED(device->CreateRenderTargetView(rt_texture_mrt1.Get(), &rt_rtv_desc, &rt_rtv_mrt1))) {
      std::fputs("Failed to create the render target 1 RTV.\n", stderr);
      return false;
   }
   D3D11_UNORDERED_ACCESS_VIEW_DESC rt_uav_desc;
   rt_uav_desc.Format = DXGI_FORMAT_R32_FLOAT;
   rt_uav_desc.ViewDimension = D3D11_UAV_DIMENSION_TEXTURE2D;
   rt_uav_desc.Texture2D.MipSlice = 0;
   Microsoft::WRL::ComPtr<ID3D11UnorderedAccessView> rt_uav;
   if (FAILED(device->CreateUnorderedAccessView(rt_texture_rat.Get(), &rt_uav_desc, &rt_uav))) {
      std::fputs("Failed to create the RAT UAV.\n", stderr);
      return false;
   }

   D3D11_RASTERIZER_DESC rasterizer_desc = {};
   rasterizer_desc.FillMode = D3D11_FILL_SOLID;
   rasterizer_desc.CullMode = D3D11_CULL_NONE;
   rasterizer_desc.DepthClipEnable = TRUE;
   Microsoft::WRL::ComPtr<ID3D11RasterizerState> rasterizer_state;
   if (FAILED(device->CreateRasterizerState(&rasterizer_desc, &rasterizer_state))) {
      std::fputs("Failed to create the rasterizer state.\n", stderr);
      return false;
   }

   D3D11_DEPTH_STENCIL_DESC depth_stencil_desc;
   depth_stencil_desc.DepthEnable = TRUE;
   depth_stencil_desc.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ALL;
   depth_stencil_desc.DepthFunc = D3D11_COMPARISON_ALWAYS;
   depth_stencil_desc.StencilEnable = TRUE;
   depth_stencil_desc.StencilReadMask = 0;
   depth_stencil_desc.StencilWriteMask = 0xFF;
   depth_stencil_desc.FrontFace.StencilFailOp = D3D11_STENCIL_OP_REPLACE;
   depth_stencil_desc.FrontFace.StencilDepthFailOp = D3D11_STENCIL_OP_REPLACE;
   depth_stencil_desc.FrontFace.StencilPassOp = D3D11_STENCIL_OP_REPLACE;
   depth_stencil_desc.FrontFace.StencilFunc = D3D11_COMPARISON_ALWAYS;
   depth_stencil_desc.BackFace = depth_stencil_desc.FrontFace;
   Microsoft::WRL::ComPtr<ID3D11DepthStencilState> depth_stencil_state;
   if (FAILED(device->CreateDepthStencilState(&depth_stencil_desc, &depth_stencil_state))) {
      std::fputs("Failed to create the depth/stencil state.\n", stderr);
      return false;
   }

   D3D11_QUERY_DESC query_desc = {};
   query_desc.Query = D3D11_QUERY_TIMESTAMP;
   Microsoft::WRL::ComPtr<ID3D11Query> timestamp_query;
   if (FAILED(device->CreateQuery(&query_desc, &timestamp_query))) {
      std::fputs("Failed to create the timestamp queue.\n", stderr);
      return false;
   }
   query_desc.Query = D3D11_QUERY_PIPELINE_STATISTICS;
   Microsoft::WRL::ComPtr<ID3D11Query> pipeline_statistics_query;
   if (FAILED(device->CreateQuery(&query_desc, &pipeline_statistics_query))) {
      std::fputs("Failed to create the pipeline statistics queue.\n", stderr);
      return false;
   }

   Microsoft::WRL::ComPtr<ID3D11DeviceContext1> context1;
   if (FAILED(context->QueryInterface(IID_PPV_ARGS(&context1)))) {
      std::fputs("Failed to query the ID3D11DeviceContext1 interface.\n", stderr);
      return false;
   }

   std::puts("Clearing multiple render targets and depth/stencil.");
   context->ClearDepthStencilView(rt_dsv.Get(), D3D11_CLEAR_DEPTH | D3D11_CLEAR_STENCIL, 1.0f, 0);
   FLOAT const rt_clear_color[4] = {};
   context->ClearRenderTargetView(rt_rtv_mrt0.Get(), rt_clear_color);
   context->ClearRenderTargetView(rt_rtv_mrt1.Get(), rt_clear_color);
   context->ClearUnorderedAccessViewFloat(rt_uav.Get(), rt_clear_color);
   std::puts("Flushing clears.");
   context->Flush();

   std::puts("Rendering to multiple render targets and a RAT with depth/stencil.");
   context->Begin(pipeline_statistics_query.Get());
   context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP);
   context->IASetIndexBuffer(index_buffer.Get(), DXGI_FORMAT_R16_UINT, sizeof(uint16_t));
   context->IASetInputLayout(input_layout.Get());
   ID3D11Buffer * const vertex_buffers[] = {vertex_buffer.Get(), vertex_buffer.Get()};
   UINT const vertex_buffer_strides[] = {sizeof(float) * 6, sizeof(float) * 6};
   UINT const vertex_buffer_offsets[] = {0, sizeof(float) * 4};
   context->IASetVertexBuffers(0, 2, vertex_buffers, vertex_buffer_strides, vertex_buffer_offsets);
   context->VSSetShader(vertex_shader.Get(), nullptr, 0);
   UINT const num_constants = 0x10;
   UINT const vs_first_constant = 0x10;
   context1->VSSetConstantBuffers1(1, 1, constant_buffer.GetAddressOf(), &vs_first_constant,
                                   &num_constants);
   context->RSSetState(rasterizer_state.Get());
   D3D11_VIEWPORT viewport = {};
   viewport.Width = static_cast<FLOAT>(rt_width);
   viewport.Height = static_cast<FLOAT>(rt_height);
   viewport.MaxDepth = 1.0f;
   context->RSSetViewports(1, &viewport);
   context->PSSetShader(pixel_shader_with_rat.Get(), nullptr, 0);
   UINT const ps_first_constant = 0x20;
   context1->PSSetConstantBuffers1(2, 1, constant_buffer.GetAddressOf(), &ps_first_constant,
                                   &num_constants);
   ID3D11RenderTargetView * const rtvs[] = {rt_rtv_mrt0.Get(), rt_rtv_mrt1.Get()};
   context->OMSetDepthStencilState(depth_stencil_state.Get(), 0xFF);
   context->OMSetBlendState(nullptr, nullptr, ~static_cast<UINT>(0));
   context->OMSetRenderTargetsAndUnorderedAccessViews(2, rtvs, rt_dsv.Get(), 2, 1,
                                                      rt_uav.GetAddressOf(), nullptr);
   context->DrawIndexed(4, 1, 0);
   context->End(pipeline_statistics_query.Get());
   context->End(timestamp_query.Get());
   std::puts("Flushing render.");
   context->Flush();

   std::puts("Writing events.");
   std::puts("Flushing render.");
   context->Flush();

   return true;
}

bool D3DR_TessellationTest(ID3D11Device * const device, ID3D11DeviceContext * const context) {
   context->ClearState();

   char const vertex_shader_source[] =
      "cbuffer cb_constants : register(b1) {\r\n"
      "   uint vertex_id_offset;\r\n"
      D3DR_SCRATCH_CONSTANTS
      "}\r\n"
      "uint main(uint vertex_id : SV_VertexID) : VERTEXID {\r\n"
      D3DR_SCRATCH_OPERATIONS
      "   return vertex_id + vertex_id_offset + temp_array[temp_index_2];\r\n"
      "}\r\n";

   char const hull_shader_source[] =
      "cbuffer cb_constants : register(b1) {\r\n"
      "   uint vertex_id_offset;\r\n"
      D3DR_SCRATCH_CONSTANTS
      "}\r\n"
      "struct patch_control_point {\r\n"
      "   uint vertex_id : VERTEXID;\r\n"
      "};\r\n"
      "struct patch_constant_data {\r\n"
      "   uint patch_id : PATCHID;\r\n"
      "   float edges[4] : SV_TessFactor;\r\n"
      "   float inside[2] : SV_InsideTessFactor;\r\n"
      "};\r\n"
      "patch_constant_data patch_constant_function(\r\n"
      "   InputPatch<patch_control_point, 1> ip,\r\n"
      "   uint patch_id : SV_PrimitiveID) {\r\n"
      "   patch_constant_data data;\r\n"
      "   data.patch_id = ip[0].vertex_id + vertex_id_offset;\r\n"
      "   data.edges[0] = 1.0;\r\n"
      "   data.edges[1] = 1.0;\r\n"
      "   data.edges[2] = 1.0;\r\n"
      "   data.edges[3] = 1.0;\r\n"
      "   data.inside[0] = 1.0;\r\n"
      "   data.inside[1] = 1.0;\r\n"
      "   return data;\r\n"
      "}\r\n"
      "[domain(\"quad\")]\r\n"
      "[partitioning(\"integer\")]\r\n"
      "[outputtopology(\"triangle_cw\")]\r\n"
      "[outputcontrolpoints(1)]\r\n"
      "[patchconstantfunc(\"patch_constant_function\")]\r\n"
      "patch_control_point main(\r\n"
      "   InputPatch<patch_control_point, 1> ip,\r\n"
      "   uint i : SV_OutputControlPointID) {\r\n"
      "   patch_control_point op = ip[i];\r\n"
      D3DR_SCRATCH_OPERATIONS
      "   op.vertex_id += temp_array[temp_index_2];\r\n"
      "   return op;\r\n"
      "}\r\n";

   char const domain_shader_source[] =
      "cbuffer cb_constants : register(b1) {\r\n"
      "   float layer_scale;\r\n"
      D3DR_SCRATCH_CONSTANTS
      "}\r\n"
      "struct patch_constant_data {\r\n"
      "   uint patch_id : PATCHID;\r\n"
      "   float edges[4] : SV_TessFactor;\r\n"
      "   float inside[2] : SV_InsideTessFactor;\r\n"
      "};\r\n"
      "struct patch_control_point {\r\n"
      "   uint vertex_id : VERTEXID;\r\n"
      "};\r\n"
      "[domain(\"quad\")]\r\n"
      "float4 main(\r\n"
      "   patch_constant_data input,\r\n"
      "   float2 uv : SV_DomainLocation,\r\n"
      "   OutputPatch<patch_control_point, 1> patch) : SV_Position {\r\n"
      D3DR_SCRATCH_OPERATIONS
      "   return float4(\r\n"
      "             uv,\r\n"
      "             float(input.patch_id + patch[0].vertex_id + temp_array[temp_index_2]) * \r\n"
      "                layer_scale,\r\n"
      "             1.0);\r\n"
      "}\r\n";

   char const geometry_shader_source[] =
      "cbuffer cb_constants : register(b1) {\r\n"
      "   uint vertex_rotation;\r\n"
      D3DR_SCRATCH_CONSTANTS
      "}\r\n"
      "struct vertex {\r\n"
      "   float4 position : SV_Position;\r\n"
      "};\r\n"
      "[maxvertexcount(3)]\r\n"
      "void main(\r\n"
      "   triangle vertex input[3] : SV_Position,\r\n"
      "   inout TriangleStream<vertex> output_stream) {\r\n"
      "   vertex output;\r\n"
      "   output.position = input[vertex_rotation % 3u].position;\r\n"
      "   uint temp_array[256] = {\r\n"
      "      0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,\r\n"
      "      0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,\r\n"
      "      0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,\r\n"
      "      0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,\r\n"
      "      0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,\r\n"
      "      0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,\r\n"
      "      0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,\r\n"
      "      0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,\r\n"
      "      0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,\r\n"
      "      0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,\r\n"
      "      0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,\r\n"
      "      0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,\r\n"
      "      0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,\r\n"
      "      0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,\r\n"
      "      0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,\r\n"
      "      0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,\r\n"
      "   };\r\n"
      "   temp_array[temp_index_0] = temp_value_0;\r\n"
      "   temp_array[temp_index_0 * 2u] = temp_value_0;\r\n"
      "   temp_array[temp_index_1] = temp_value_1;\r\n"
      "   temp_array[temp_index_1 + 1u] = temp_value_1;\r\n"
      "   output.position.z += float(temp_array[temp_index_2]);\r\n"
      "   output_stream.Append(output);\r\n"
      "   output.position = input[(vertex_rotation + 1u) % 3u].position;\r\n"
      "   output_stream.Append(output);\r\n"
      "   output.position = input[(vertex_rotation + 2u) % 3u].position;\r\n"
      "   output_stream.Append(output);\r\n"
      "   output_stream.RestartStrip();\r\n"
      "}\r\n";

   char const pixel_shader_source[] =
      "cbuffer cb_constants : register(b1) {\r\n"
      D3DR_SCRATCH_CONSTANTS
      "}\r\n"
      "float4 main() : SV_Target0 {\r\n"
      D3DR_SCRATCH_OPERATIONS
      "   return float4(temp_array[temp_index_2], 1.0, 1.0, 1.0);\r\n"
      "}\r\n";

   char const compute_shader_source[] =
      "cbuffer cb_constants : register(b1) {\r\n"
      D3DR_SCRATCH_CONSTANTS
      "}\r\n"
      "Texture2DMS<float4> in_srv : register(t2);\r\n"
      "RWTexture2D<unorm float4> out_uav : register(u3);\r\n"
      "[numthreads(8, 8, 1)]\r\n"
      "void main(uint3 dtid : SV_DispatchThreadID) {\r\n"
      D3DR_SCRATCH_OPERATIONS
      "   out_uav[dtid.xy] = in_srv.Load(int2(dtid.xy), 0) + float(temp_array[temp_index_2]);\r\n"
      "}\r\n";

   Microsoft::WRL::ComPtr<ID3DBlob> vertex_shader_bytecode =
      D3DR_CompileShader(vertex_shader_source, sizeof(vertex_shader_source) - 1, "vs_5_0");
   if (!vertex_shader_bytecode) {
      return false;
   }
   Microsoft::WRL::ComPtr<ID3D11VertexShader> vertex_shader;
   if (FAILED(device->CreateVertexShader(vertex_shader_bytecode->GetBufferPointer(),
                                         vertex_shader_bytecode->GetBufferSize(), nullptr,
                                         &vertex_shader))) {
      std::fputs("Failed to create the tessellation vertex shader.\n", stderr);
      return false;
   }

   Microsoft::WRL::ComPtr<ID3DBlob> hull_shader_bytecode =
      D3DR_CompileShader(hull_shader_source, sizeof(hull_shader_source) - 1, "hs_5_0");
   if (!hull_shader_bytecode) {
      return false;
   }
   Microsoft::WRL::ComPtr<ID3D11HullShader> hull_shader;
   if (FAILED(device->CreateHullShader(hull_shader_bytecode->GetBufferPointer(),
                                       hull_shader_bytecode->GetBufferSize(), nullptr,
                                       &hull_shader))) {
      std::fputs("Failed to create the tessellation hull shader.\n", stderr);
      return false;
   }

   Microsoft::WRL::ComPtr<ID3DBlob> domain_shader_bytecode =
      D3DR_CompileShader(domain_shader_source, sizeof(domain_shader_source) - 1, "ds_5_0");
   if (!domain_shader_bytecode) {
      return false;
   }
   Microsoft::WRL::ComPtr<ID3D11DomainShader> domain_shader;
   if (FAILED(device->CreateDomainShader(domain_shader_bytecode->GetBufferPointer(),
                                         domain_shader_bytecode->GetBufferSize(), nullptr,
                                         &domain_shader))) {
      std::fputs("Failed to create the tessellation domain shader.\n", stderr);
      return false;
   }

   Microsoft::WRL::ComPtr<ID3DBlob> geometry_shader_bytecode =
      D3DR_CompileShader(geometry_shader_source, sizeof(geometry_shader_source) - 1, "gs_5_0");
   if (!geometry_shader_bytecode) {
      return false;
   }
   Microsoft::WRL::ComPtr<ID3D11GeometryShader> geometry_shader;
   if (FAILED(device->CreateGeometryShader(geometry_shader_bytecode->GetBufferPointer(),
                                           geometry_shader_bytecode->GetBufferSize(), nullptr,
                                           &geometry_shader))) {
      std::fputs("Failed to create the tessellation geometry shader.\n", stderr);
      return false;
   }

   Microsoft::WRL::ComPtr<ID3DBlob> pixel_shader_bytecode =
      D3DR_CompileShader(pixel_shader_source, sizeof(pixel_shader_source) - 1, "ps_5_0");
   if (!pixel_shader_bytecode) {
      return false;
   }
   Microsoft::WRL::ComPtr<ID3D11PixelShader> pixel_shader;
   if (FAILED(device->CreatePixelShader(pixel_shader_bytecode->GetBufferPointer(),
                                        pixel_shader_bytecode->GetBufferSize(), nullptr,
                                        &pixel_shader))) {
      std::fputs("Failed to create the tessellation pixel shader.\n", stderr);
      return false;
   }

   Microsoft::WRL::ComPtr<ID3DBlob> compute_shader_bytecode =
      D3DR_CompileShader(compute_shader_source, sizeof(compute_shader_source) - 1, "cs_5_0");
   if (!compute_shader_bytecode) {
      return false;
   }
   Microsoft::WRL::ComPtr<ID3D11ComputeShader> compute_shader;
   if (FAILED(device->CreateComputeShader(compute_shader_bytecode->GetBufferPointer(),
                                          compute_shader_bytecode->GetBufferSize(), nullptr,
                                          &compute_shader))) {
      std::fputs("Failed to create the compute shader.\n", stderr);
      return false;
   }

   float constant_buffer_data[0x40] = {};
   D3D11_SUBRESOURCE_DATA constant_buffer_initial_data = {};
   constant_buffer_initial_data.pSysMem = constant_buffer_data;
   D3D11_BUFFER_DESC constant_buffer_desc = {};
   constant_buffer_desc.ByteWidth = sizeof(constant_buffer_data);
   constant_buffer_desc.Usage = D3D11_USAGE_IMMUTABLE;
   constant_buffer_desc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
   Microsoft::WRL::ComPtr<ID3D11Buffer> constant_buffer;
   if (FAILED(device->CreateBuffer(&constant_buffer_desc, &constant_buffer_initial_data,
                                   &constant_buffer))) {
      std::fputs("Failed to create the constant buffer.\n", stderr);
      return false;
   }

   uint32_t const rt_width = 1280;
   uint32_t const rt_height = 720;

   D3D11_TEXTURE2D_DESC rt_texture_desc = {};
   rt_texture_desc.Width = rt_width;
   rt_texture_desc.Height = rt_height;
   rt_texture_desc.MipLevels = 1;
   rt_texture_desc.ArraySize = 1;
   rt_texture_desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
   rt_texture_desc.SampleDesc.Count = 4;
   rt_texture_desc.Usage = D3D11_USAGE_DEFAULT;
   rt_texture_desc.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET;
   Microsoft::WRL::ComPtr<ID3D11Texture2D> rt_texture_ms;
   if (FAILED(device->CreateTexture2D(&rt_texture_desc, nullptr, &rt_texture_ms))) {
      std::fputs("Failed to create the multisampled texture.\n", stderr);
      return false;
   }
   rt_texture_desc.SampleDesc.Count = 1;
   rt_texture_desc.BindFlags = 0;
   Microsoft::WRL::ComPtr<ID3D11Texture2D> rt_texture_resolve;
   if (FAILED(device->CreateTexture2D(&rt_texture_desc, nullptr, &rt_texture_resolve))) {
      std::fputs("Failed to create the resolve destination texture.\n", stderr);
      return false;
   }
   rt_texture_desc.BindFlags = D3D11_BIND_UNORDERED_ACCESS;
   Microsoft::WRL::ComPtr<ID3D11Texture2D> rt_texture_uav;
   if (FAILED(device->CreateTexture2D(&rt_texture_desc, nullptr, &rt_texture_uav))) {
      std::fputs("Failed to create the compute UAV.\n", stderr);
      return false;
   }

   D3D11_RENDER_TARGET_VIEW_DESC rt_rtv_desc;
   rt_rtv_desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
   rt_rtv_desc.ViewDimension = D3D11_RTV_DIMENSION_TEXTURE2DMS;
   Microsoft::WRL::ComPtr<ID3D11RenderTargetView> rt_rtv_ms;
   if (FAILED(device->CreateRenderTargetView(rt_texture_ms.Get(), &rt_rtv_desc, &rt_rtv_ms))) {
      std::fputs("Failed to create the multisampled RTV.\n", stderr);
      return false;
   }

   D3D11_SHADER_RESOURCE_VIEW_DESC rt_srv_desc;
   rt_srv_desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
   rt_srv_desc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2DMS;
   Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> rt_srv;
   if (FAILED(device->CreateShaderResourceView(rt_texture_ms.Get(), &rt_srv_desc, &rt_srv))) {
      std::fputs("Failed to create the multisampled SRV.\n", stderr);
      return false;
   }

   D3D11_UNORDERED_ACCESS_VIEW_DESC rt_uav_desc;
   rt_uav_desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
   rt_uav_desc.ViewDimension = D3D11_UAV_DIMENSION_TEXTURE2D;
   rt_uav_desc.Texture2D.MipSlice = 0;
   Microsoft::WRL::ComPtr<ID3D11UnorderedAccessView> rt_uav;
   if (FAILED(device->CreateUnorderedAccessView(rt_texture_uav.Get(), &rt_uav_desc, &rt_uav))) {
      std::fputs("Failed to create the UAV.\n", stderr);
      return false;
   }

   D3D11_RASTERIZER_DESC rasterizer_desc = {};
   rasterizer_desc.FillMode = D3D11_FILL_SOLID;
   rasterizer_desc.CullMode = D3D11_CULL_NONE;
   rasterizer_desc.DepthClipEnable = TRUE;
   Microsoft::WRL::ComPtr<ID3D11RasterizerState> rasterizer_state;
   if (FAILED(device->CreateRasterizerState(&rasterizer_desc, &rasterizer_state))) {
      std::fputs("Failed to create the rasterizer state.\n", stderr);
      return false;
   }

   D3D11_DRAW_INSTANCED_INDIRECT_ARGS indirect_draw_buffer_data = {};
   indirect_draw_buffer_data.VertexCountPerInstance = 1;
   indirect_draw_buffer_data.InstanceCount = 1;
   D3D11_SUBRESOURCE_DATA indirect_draw_buffer_initial_data = {};
   indirect_draw_buffer_initial_data.pSysMem = &indirect_draw_buffer_data;
   D3D11_BUFFER_DESC indirect_draw_buffer_desc = {};
   indirect_draw_buffer_desc.ByteWidth = sizeof(indirect_draw_buffer_data);
   indirect_draw_buffer_desc.Usage = D3D11_USAGE_DEFAULT;
   indirect_draw_buffer_desc.MiscFlags = D3D11_RESOURCE_MISC_DRAWINDIRECT_ARGS;
   Microsoft::WRL::ComPtr<ID3D11Buffer> indirect_draw_buffer;
   if (FAILED(device->CreateBuffer(&indirect_draw_buffer_desc, &indirect_draw_buffer_initial_data,
                                   &indirect_draw_buffer))) {
      std::fputs("Failed to create the indirect draw argument buffer.\n", stderr);
      return false;
   }

   uint32_t const indirect_dispatch_buffer_data[3] = {(rt_width + 7) / 8, (rt_height + 7) / 8, 1};
   D3D11_SUBRESOURCE_DATA indirect_dispatch_buffer_initial_data = {};
   indirect_dispatch_buffer_initial_data.pSysMem = indirect_dispatch_buffer_data;
   D3D11_BUFFER_DESC indirect_dispatch_buffer_desc = {};
   indirect_dispatch_buffer_desc.ByteWidth = sizeof(indirect_dispatch_buffer_data);
   indirect_dispatch_buffer_desc.Usage = D3D11_USAGE_DEFAULT;
   indirect_dispatch_buffer_desc.MiscFlags = D3D11_RESOURCE_MISC_DRAWINDIRECT_ARGS;
   Microsoft::WRL::ComPtr<ID3D11Buffer> indirect_dispatch_buffer;
   if (FAILED(device->CreateBuffer(&indirect_dispatch_buffer_desc, &indirect_dispatch_buffer_initial_data,
                                   &indirect_dispatch_buffer))) {
      std::fputs("Failed to create the indirect dispatch argument buffer.\n", stderr);
      return false;
   }

   std::puts("Rendering with tessellation and geometry shaders and resolving.");
   context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_1_CONTROL_POINT_PATCHLIST);
   context->VSSetShader(vertex_shader.Get(), nullptr, 0);
   context->VSSetConstantBuffers(1, 1, constant_buffer.GetAddressOf());
   context->HSSetShader(hull_shader.Get(), nullptr, 0);
   context->HSSetConstantBuffers(1, 1, constant_buffer.GetAddressOf());
   context->DSSetShader(domain_shader.Get(), nullptr, 0);
   context->DSSetConstantBuffers(1, 1, constant_buffer.GetAddressOf());
   context->GSSetShader(geometry_shader.Get(), nullptr, 0);
   context->GSSetConstantBuffers(1, 1, constant_buffer.GetAddressOf());
   context->RSSetState(rasterizer_state.Get());
   D3D11_VIEWPORT viewport = {};
   viewport.Width = static_cast<FLOAT>(rt_width);
   viewport.Height = static_cast<FLOAT>(rt_height);
   viewport.MaxDepth = 1.0f;
   context->RSSetViewports(1, &viewport);
   context->PSSetShader(pixel_shader.Get(), nullptr, 0);
   context->PSSetConstantBuffers(1, 1, constant_buffer.GetAddressOf());
   context->OMSetRenderTargets(1, rt_rtv_ms.GetAddressOf(), nullptr);
   context->DrawInstancedIndirect(indirect_draw_buffer.Get(), 0);
   context->ResolveSubresource(rt_texture_resolve.Get(), 0, rt_texture_ms.Get(), 0,
                               DXGI_FORMAT_R8G8B8A8_UNORM);
   context->CSSetShader(compute_shader.Get(), nullptr, 0);
   context->CSSetConstantBuffers(1, 1, constant_buffer.GetAddressOf());
   context->CSSetShaderResources(2, 1, rt_srv.GetAddressOf());
   context->CSSetUnorderedAccessViews(3, 1, rt_uav.GetAddressOf(), nullptr);
   context->DispatchIndirect(indirect_dispatch_buffer.Get(), 0);
   std::puts("Flushing render.");
   context->Flush();

   return true;
}

int main(int const argc, char const * const argv[]) {
   KMTI_Begin();

   HMODULE dxgi_module = LoadLibraryW(L"dxgi.dll");
   if (dxgi_module == nullptr) {
      std::fputs("Failed to load dxgi.dll.\n", stderr);
      return EXIT_FAILURE;
   }
   auto const create_dxgi_factory =
      (decltype(&CreateDXGIFactory))(GetProcAddress(dxgi_module, "CreateDXGIFactory"));
   if (create_dxgi_factory == nullptr) {
      std::fputs("Failed to get CreateDXGIFactory.\n", stderr);
   }

   HMODULE d3d11_module = LoadLibraryW(L"d3d11.dll");
   if (d3d11_module == nullptr) {
      std::fputs("Failed to load dxgi.dll.\n", stderr);
      return EXIT_FAILURE;
   }
   auto const d3d11_create_device =
      (decltype(&D3D11CreateDevice))(GetProcAddress(d3d11_module, "D3D11CreateDevice"));
   if (d3d11_create_device == nullptr) {
      std::fputs("Failed to get D3D11CreateDevice.\n", stderr);
   }

   ComPtr<IDXGIFactory> dxgi_factory;
   if (FAILED(create_dxgi_factory(IID_PPV_ARGS(&dxgi_factory)))) {
      std::fputs("Failed to create a DXGI factory.\n", stderr);
      return EXIT_FAILURE;
   }

   ComPtr<IDXGIAdapter> dxgi_adapter;
   for (UINT dxgi_adapter_index = 0;; ++dxgi_adapter_index) {
      ComPtr<IDXGIAdapter> current_dxgi_adapter;
      if (FAILED(dxgi_factory->EnumAdapters(dxgi_adapter_index, &current_dxgi_adapter))) {
         break;
      }
      DXGI_ADAPTER_DESC dxgi_adapter_desc;
      if (FAILED(current_dxgi_adapter->GetDesc(&dxgi_adapter_desc))) {
         continue;
      }
      if (dxgi_adapter_desc.VendorId != 0x1002) {
         continue;
      }
      dxgi_adapter = std::move(current_dxgi_adapter);
      break;
   }
   if (!dxgi_adapter) {
      std::fputs("Failed to get an AMD DXGI adapter.\n", stderr);
      return EXIT_FAILURE;
   }

   D3D_FEATURE_LEVEL const feature_level = D3D_FEATURE_LEVEL_11_0;
   ComPtr<ID3D11Device> device;
   ComPtr<ID3D11DeviceContext> context;
   if (FAILED(d3d11_create_device(dxgi_adapter.Get(), D3D_DRIVER_TYPE_UNKNOWN, nullptr, 0,
                                  &feature_level, 1, D3D11_SDK_VERSION, &device, nullptr,
                                  &context))) {
      std::fputs("Failed to create a Direct3D 11 device.\n", stderr);
      return EXIT_FAILURE;
   }

   /* GenerateMips invocation (draws likely). */
   {
      char const texture_initial_data_mem[0x80 * 0x80 * 4] = {};
      D3D11_SUBRESOURCE_DATA const texture_initial_data[] = {
         {texture_initial_data_mem, 0x80 * 4},
         {texture_initial_data_mem, 0x40 * 4},
         {texture_initial_data_mem, 0x20 * 4},
         {texture_initial_data_mem, 0x10 * 4},
         {texture_initial_data_mem, 0x8 * 4},
         {texture_initial_data_mem, 0x4 * 4},
         {texture_initial_data_mem, 0x2 * 4},
         {texture_initial_data_mem, 0x1 * 4},
      };
      D3D11_TEXTURE2D_DESC texture_desc = {};
      texture_desc.Width = 128;
      texture_desc.Height = 128;
      texture_desc.MipLevels = sizeof(texture_initial_data) / sizeof(texture_initial_data[0]);
      texture_desc.ArraySize = 1;
      texture_desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
      texture_desc.SampleDesc.Count = 1;
      texture_desc.Usage = D3D11_USAGE_DEFAULT;
      texture_desc.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET;
      texture_desc.MiscFlags = D3D11_RESOURCE_MISC_GENERATE_MIPS;
      ComPtr<ID3D11Texture2D> texture;
      if (FAILED(device->CreateTexture2D(&texture_desc, texture_initial_data, &texture))) {
         std::fputs("Failed to create the texture.\n", stderr);
         return EXIT_FAILURE;
      }

      D3D11_SHADER_RESOURCE_VIEW_DESC texture_srv_desc = {};
      texture_srv_desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
      texture_srv_desc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
      texture_srv_desc.Texture2D.MostDetailedMip = 0;
      texture_srv_desc.Texture2D.MipLevels = static_cast<UINT>(-1);
      ComPtr<ID3D11ShaderResourceView> texture_srv;
      if (FAILED(device->CreateShaderResourceView(texture.Get(), &texture_srv_desc,
                                                  &texture_srv))) {
         std::fputs("Failed to create the texture shader resource view.\n", stderr);
         return EXIT_FAILURE;
      }

      std::puts("Generating mipmaps.");

      context->GenerateMips(texture_srv.Get());

      std::puts("Flushing GenerateMips.");
      context->Flush();
   }

   /* Allocation private data research. */

   {
      ComPtr<ID3D11Buffer> buffer;

      D3D11_BUFFER_DESC buffer_desc = {};
      buffer_desc.ByteWidth = 0x1337;
      buffer_desc.StructureByteStride = 4;

      std::puts("Allocating a DEFAULT unordered access buffer.");
      buffer_desc.Usage = D3D11_USAGE_DEFAULT;
      buffer_desc.BindFlags = D3D11_BIND_UNORDERED_ACCESS;
      buffer_desc.CPUAccessFlags = 0;
      if (FAILED(device->CreateBuffer(&buffer_desc, nullptr, &buffer))) {
         std::fputs("Failed to create a buffer.\n", stderr);
         return EXIT_FAILURE;
      }

      std::puts("Allocating a STAGING read/write buffer.");
      buffer_desc.Usage = D3D11_USAGE_STAGING;
      buffer_desc.BindFlags = 0;
      buffer_desc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE | D3D11_CPU_ACCESS_READ;
      if (FAILED(device->CreateBuffer(&buffer_desc, nullptr, &buffer))) {
         std::fputs("Failed to create a buffer.\n", stderr);
         return EXIT_FAILURE;
      }

      std::puts("Allocating a DYNAMIC shader resource buffer.");
      buffer_desc.Usage = D3D11_USAGE_DYNAMIC;
      buffer_desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
      buffer_desc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
      if (FAILED(device->CreateBuffer(&buffer_desc, nullptr, &buffer))) {
         std::fputs("Failed to create a buffer.\n", stderr);
         return EXIT_FAILURE;
      }
   }

   if (!D3DR_RenderTest(device.Get(), context.Get())) {
      return EXIT_FAILURE;
   }

   if (!D3DR_TessellationTest(device.Get(), context.Get())) {
      return EXIT_FAILURE;
   }

   return EXIT_SUCCESS;
}
