/**
* This file is part of Special K.
*
* Special K is free software : you can redistribute it
* and/or modify it under the terms of the GNU General Public License
* as published by The Free Software Foundation, either version 3 of
* the License, or (at your option) any later version.
*
* Special K is distributed in the hope that it will be useful,
*
* But WITHOUT ANY WARRANTY; without even the implied warranty of
* MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
* GNU General Public License for more details.
*
* You should have received a copy of the GNU General Public License
* along with Special K.
*
*   If not, see <http://www.gnu.org/licenses/>.
*
**/

#include <SpecialK/stdafx.h>

#ifdef  __SK_SUBSYSTEM__
#undef  __SK_SUBSYSTEM__
#endif
#define __SK_SUBSYSTEM__ L"DXGI Util."

#include <SpecialK/render/dxgi/dxgi_interfaces.h>

//
// Helpers for typeless DXGI format class view compatibility
//
//   Returns true if this is a valid way of (re)interpreting a sized datatype
bool
SK_DXGI_IsDataSizeClassOf ( DXGI_FORMAT typeless, DXGI_FORMAT typed,
                            int         recursion )
{
  switch (typeless)
  {
    case DXGI_FORMAT_R24G8_TYPELESS:
    case DXGI_FORMAT_X24_TYPELESS_G8_UINT:
    case DXGI_FORMAT_R24_UNORM_X8_TYPELESS:
      return true;

    case DXGI_FORMAT_D24_UNORM_S8_UINT:
      return ( typed == DXGI_FORMAT_D32_FLOAT_S8X24_UINT );
  }

  if (recursion == 0)
    return SK_DXGI_IsDataSizeClassOf (typed, typeless, 1);

  return false;
}

DXGI_FORMAT
SK_DXGI_MakeTypedFormat (DXGI_FORMAT typeless)
{
#if 1
  return
    DirectX::MakeTypelessUNORM   (
      DirectX::MakeTypelessFLOAT (typeless)
                                 );
#else
  switch (typeless)
  {
    // Non-renderable formats
    case DXGI_FORMAT_BC1_TYPELESS:
      return DXGI_FORMAT_BC1_UNORM;
    case DXGI_FORMAT_BC2_TYPELESS:
      return DXGI_FORMAT_BC2_UNORM;
    case DXGI_FORMAT_BC3_TYPELESS:
      return DXGI_FORMAT_BC3_UNORM;
    case DXGI_FORMAT_BC4_TYPELESS:
      return DXGI_FORMAT_BC4_UNORM;
    case DXGI_FORMAT_BC5_TYPELESS:
      return DXGI_FORMAT_BC5_UNORM;
    case DXGI_FORMAT_BC6H_TYPELESS:
      return DXGI_FORMAT_BC6H_SF16;
    case DXGI_FORMAT_BC7_TYPELESS:
      return DXGI_FORMAT_BC7_UNORM;

    case DXGI_FORMAT_B8G8R8A8_TYPELESS:
      return DXGI_FORMAT_B8G8R8A8_UNORM;
    case DXGI_FORMAT_R8G8B8A8_TYPELESS:
      return DXGI_FORMAT_R8G8B8A8_UNORM;

    case DXGI_FORMAT_R8_TYPELESS:
      return DXGI_FORMAT_R8_UNORM;
    case DXGI_FORMAT_R8G8_TYPELESS:
      return DXGI_FORMAT_R8G8_UNORM;

    case DXGI_FORMAT_R16_TYPELESS:
      return DXGI_FORMAT_R16_FLOAT;
    case DXGI_FORMAT_R16G16_TYPELESS:
      return DXGI_FORMAT_R16G16_FLOAT;
    case DXGI_FORMAT_R16G16B16A16_TYPELESS:
      return DXGI_FORMAT_R16G16B16A16_FLOAT;

    case DXGI_FORMAT_R32_TYPELESS:
      return DXGI_FORMAT_R32_FLOAT;
    case DXGI_FORMAT_R32G32_TYPELESS:
      return DXGI_FORMAT_R32G32_FLOAT;
    case DXGI_FORMAT_R32G32B32_TYPELESS:
      return DXGI_FORMAT_R32G32B32_FLOAT;
    case DXGI_FORMAT_R32G32B32A32_TYPELESS:
      return DXGI_FORMAT_R32G32B32A32_FLOAT;


    case DXGI_FORMAT_R10G10B10A2_TYPELESS:
      return DXGI_FORMAT_R10G10B10A2_UNORM;

    // Generally a depth-stencil texture, so treat as UNORM : UINT
    case DXGI_FORMAT_R24G8_TYPELESS:
      return DXGI_FORMAT_D24_UNORM_S8_UINT;

    // Depth view, stencil is hard to get at
    case DXGI_FORMAT_R32G8X24_TYPELESS:
    case DXGI_FORMAT_R32_FLOAT_X8X24_TYPELESS:
      return DXGI_FORMAT_R32G32_FLOAT;


    default:
      return typeless;
  };
#endif
}

DXGI_FORMAT
SK_DXGI_MakeTypelessFormat (DXGI_FORMAT typed)
{
#if 1
  return
    DirectX::MakeTypeless (typed);
#else
    switch (typed)
  {
    case DXGI_FORMAT_BC1_UNORM:
    case DXGI_FORMAT_BC1_UNORM_SRGB:
      return DXGI_FORMAT_BC1_TYPELESS;
    case DXGI_FORMAT_BC2_UNORM:
    case DXGI_FORMAT_BC2_UNORM_SRGB:
      return DXGI_FORMAT_BC2_TYPELESS;
    case DXGI_FORMAT_BC3_UNORM:
    case DXGI_FORMAT_BC3_UNORM_SRGB:
      return DXGI_FORMAT_BC3_TYPELESS;
    case DXGI_FORMAT_BC4_UNORM:
      return DXGI_FORMAT_BC4_TYPELESS;
    case DXGI_FORMAT_BC5_UNORM:
      return DXGI_FORMAT_BC5_TYPELESS;
    case DXGI_FORMAT_BC6H_SF16:
    case DXGI_FORMAT_BC6H_UF16:
      return DXGI_FORMAT_BC6H_TYPELESS;
    case DXGI_FORMAT_BC7_UNORM:
    case DXGI_FORMAT_BC7_UNORM_SRGB:
      return DXGI_FORMAT_BC7_TYPELESS;

    case DXGI_FORMAT_B8G8R8A8_UNORM:
    case DXGI_FORMAT_B8G8R8A8_UNORM_SRGB:
      return DXGI_FORMAT_B8G8R8A8_TYPELESS;

    case DXGI_FORMAT_R8G8B8A8_UNORM:
    case DXGI_FORMAT_R8G8B8A8_UNORM_SRGB:
    case DXGI_FORMAT_R8G8B8A8_SNORM:
    case DXGI_FORMAT_R8G8B8A8_SINT:
    case DXGI_FORMAT_R8G8B8A8_UINT:
      return DXGI_FORMAT_R8G8B8A8_TYPELESS;

    case DXGI_FORMAT_B8G8R8X8_UNORM:
    case DXGI_FORMAT_B8G8R8X8_UNORM_SRGB:
      return DXGI_FORMAT_B8G8R8A8_TYPELESS;

    case DXGI_FORMAT_R10G10B10A2_UNORM:
      return DXGI_FORMAT_R10G10B10A2_TYPELESS;

    case DXGI_FORMAT_D24_UNORM_S8_UINT:
      return DXGI_FORMAT_R24G8_TYPELESS;

    case DXGI_FORMAT_R8_UNORM:
    case DXGI_FORMAT_R8_SNORM:
    case DXGI_FORMAT_R8_SINT:
    case DXGI_FORMAT_R8_UINT:
      return DXGI_FORMAT_R8_TYPELESS;

    case DXGI_FORMAT_R8G8_UNORM:
    case DXGI_FORMAT_R8G8_SNORM:
    case DXGI_FORMAT_R8G8_SINT:
    case DXGI_FORMAT_R8G8_UINT:
      return DXGI_FORMAT_R8G8_TYPELESS;

    case DXGI_FORMAT_R16_FLOAT:
    case DXGI_FORMAT_R16_UNORM:
    case DXGI_FORMAT_R16_SNORM:
    case DXGI_FORMAT_R16_SINT:
    case DXGI_FORMAT_R16_UINT:
      return DXGI_FORMAT_R16_TYPELESS;

    case DXGI_FORMAT_R16G16_UNORM:
    case DXGI_FORMAT_R16G16_SNORM:
    case DXGI_FORMAT_R16G16_FLOAT:
    case DXGI_FORMAT_R16G16_SINT:
    case DXGI_FORMAT_R16G16_UINT:
      return DXGI_FORMAT_R16G16_TYPELESS;

    case DXGI_FORMAT_R16G16B16A16_FLOAT:
    case DXGI_FORMAT_R16G16B16A16_SINT:
    case DXGI_FORMAT_R16G16B16A16_UINT:
    case DXGI_FORMAT_R16G16B16A16_UNORM:
    case DXGI_FORMAT_R16G16B16A16_SNORM:
      return DXGI_FORMAT_R16G16B16A16_TYPELESS;


    case DXGI_FORMAT_R32_FLOAT:
    case DXGI_FORMAT_R32_SINT:
    case DXGI_FORMAT_R32_UINT:
      return DXGI_FORMAT_R32_TYPELESS;

    case DXGI_FORMAT_R32G32_FLOAT:
    case DXGI_FORMAT_R32G32_SINT:
    case DXGI_FORMAT_R32G32_UINT:
      return DXGI_FORMAT_R32G32_TYPELESS;

    case DXGI_FORMAT_R32G32B32_FLOAT:
    case DXGI_FORMAT_R32G32B32_SINT:
    case DXGI_FORMAT_R32G32B32_UINT:
      return DXGI_FORMAT_R32G32B32_TYPELESS;

    case DXGI_FORMAT_R32G32B32A32_FLOAT:
    case DXGI_FORMAT_R32G32B32A32_SINT:
    case DXGI_FORMAT_R32G32B32A32_UINT:
      return DXGI_FORMAT_R32G32B32A32_TYPELESS;

    default:
      return typed;
  };
#endif
}

BOOL
__stdcall
SK_DXGI_IsFormatCompressed (DXGI_FORMAT fmt)
{
  switch (fmt)
  {
    case DXGI_FORMAT_BC1_TYPELESS:
    case DXGI_FORMAT_BC1_UNORM:
    case DXGI_FORMAT_BC1_UNORM_SRGB:
    case DXGI_FORMAT_BC2_TYPELESS:
    case DXGI_FORMAT_BC2_UNORM:
    case DXGI_FORMAT_BC2_UNORM_SRGB:
    case DXGI_FORMAT_BC3_TYPELESS:
    case DXGI_FORMAT_BC3_UNORM:
    case DXGI_FORMAT_BC3_UNORM_SRGB:
    case DXGI_FORMAT_BC4_TYPELESS:
    case DXGI_FORMAT_BC4_UNORM:
    case DXGI_FORMAT_BC4_SNORM:
    case DXGI_FORMAT_BC5_TYPELESS:
    case DXGI_FORMAT_BC5_UNORM:
    case DXGI_FORMAT_BC5_SNORM:
    case DXGI_FORMAT_BC6H_TYPELESS:
    case DXGI_FORMAT_BC6H_UF16:
    case DXGI_FORMAT_BC6H_SF16:
    case DXGI_FORMAT_BC7_TYPELESS:
    case DXGI_FORMAT_BC7_UNORM:
    case DXGI_FORMAT_BC7_UNORM_SRGB:
      return TRUE;

    default:
      return FALSE;
  }
}

BOOL
__stdcall
SK_DXGI_IsFormatFloat (DXGI_FORMAT fmt) noexcept
{
  switch (fmt)
  {
    case DXGI_FORMAT_R16_FLOAT:
    case DXGI_FORMAT_R16G16_FLOAT:
    case DXGI_FORMAT_R16G16B16A16_FLOAT:
    case DXGI_FORMAT_R32_FLOAT:
    case DXGI_FORMAT_R32G32_FLOAT:
    case DXGI_FORMAT_R32G32B32_FLOAT:
    case DXGI_FORMAT_R32G32B32A32_FLOAT:
    case DXGI_FORMAT_R11G11B10_FLOAT:
    case DXGI_FORMAT_D32_FLOAT:
    case DXGI_FORMAT_D32_FLOAT_S8X24_UINT:
    case DXGI_FORMAT_R32_FLOAT_X8X24_TYPELESS:
      return TRUE;
    default:
     return FALSE;
  };
}

BOOL
__stdcall
SK_DXGI_IsFormatInteger (DXGI_FORMAT fmt) noexcept
{
  switch (fmt)
  {
    case DXGI_FORMAT_R8G8B8A8_SINT:
    case DXGI_FORMAT_R8G8B8A8_UINT:
    case DXGI_FORMAT_R8_SINT:
    case DXGI_FORMAT_R8_UINT:
    case DXGI_FORMAT_D24_UNORM_S8_UINT:
    case DXGI_FORMAT_R8G8_SINT:
    case DXGI_FORMAT_R8G8_UINT:
    case DXGI_FORMAT_R16_SINT:
    case DXGI_FORMAT_R16_UINT:
    case DXGI_FORMAT_R16G16_SINT:
    case DXGI_FORMAT_R16G16_UINT:
    case DXGI_FORMAT_R16G16B16A16_SINT:
    case DXGI_FORMAT_R16G16B16A16_UINT:
    case DXGI_FORMAT_R32_SINT:
    case DXGI_FORMAT_R32_UINT:
    case DXGI_FORMAT_R32G32_SINT:
    case DXGI_FORMAT_R32G32_UINT:
    case DXGI_FORMAT_R32G32B32_SINT:
    case DXGI_FORMAT_R32G32B32_UINT:
    case DXGI_FORMAT_R32G32B32A32_SINT:
    case DXGI_FORMAT_R32G32B32A32_UINT:
      return TRUE;

    default:
      return FALSE;
  };
}

BOOL
__stdcall
SK_DXGI_IsFormatNormalized (DXGI_FORMAT fmt) noexcept
{
  switch (fmt)
  {
    case DXGI_FORMAT_R16G16B16A16_UNORM:
    case DXGI_FORMAT_R16G16B16A16_SNORM:
    case DXGI_FORMAT_R10G10B10A2_UNORM:
    case DXGI_FORMAT_R8G8B8A8_UNORM:
    case DXGI_FORMAT_R8G8B8A8_UNORM_SRGB:
    case DXGI_FORMAT_R8G8B8A8_SNORM:
    case DXGI_FORMAT_R16G16_UNORM:
    case DXGI_FORMAT_R16G16_SNORM:
    case DXGI_FORMAT_R8G8_UNORM:
    case DXGI_FORMAT_R8G8_SNORM:
    case DXGI_FORMAT_D16_UNORM:
    case DXGI_FORMAT_R16_UNORM:
    case DXGI_FORMAT_R16_SNORM:
    case DXGI_FORMAT_R8_UNORM:
    case DXGI_FORMAT_R8_SNORM:
    case DXGI_FORMAT_A8_UNORM:
    case DXGI_FORMAT_R1_UNORM:
    case DXGI_FORMAT_R8G8_B8G8_UNORM:
    case DXGI_FORMAT_G8R8_G8B8_UNORM:
    case DXGI_FORMAT_BC1_UNORM:
    case DXGI_FORMAT_BC1_UNORM_SRGB:
    case DXGI_FORMAT_BC2_UNORM:
    case DXGI_FORMAT_BC2_UNORM_SRGB:
    case DXGI_FORMAT_BC3_UNORM:
    case DXGI_FORMAT_BC3_UNORM_SRGB:
    case DXGI_FORMAT_BC4_UNORM:
    case DXGI_FORMAT_BC4_SNORM:
    case DXGI_FORMAT_BC5_UNORM:
    case DXGI_FORMAT_BC5_SNORM:
    case DXGI_FORMAT_B5G6R5_UNORM:
    case DXGI_FORMAT_B5G5R5A1_UNORM:
    case DXGI_FORMAT_B8G8R8A8_UNORM:
    case DXGI_FORMAT_B8G8R8X8_UNORM:
    case DXGI_FORMAT_R10G10B10_XR_BIAS_A2_UNORM:
    case DXGI_FORMAT_B8G8R8A8_UNORM_SRGB:
    case DXGI_FORMAT_B8G8R8X8_UNORM_SRGB:
    case DXGI_FORMAT_BC7_UNORM:
    case DXGI_FORMAT_BC7_UNORM_SRGB:
    case DXGI_FORMAT_B4G4R4A4_UNORM:
      return TRUE;
    default:
      return FALSE;
  }
}

INT
__stdcall
SK_DXGI_BytesPerPixel (DXGI_FORMAT fmt)
{
  static constexpr INT SixteenBytes   = 16;
  static constexpr INT TwelveBytes    = 12;
  static constexpr INT EightBytes     =  8;
  static constexpr INT FourBytes      =  4;
  static constexpr INT TwoBytes       =  2;
  static constexpr INT OneByte        =  1;
  static constexpr INT BlockComp_Half = -1;
  static constexpr INT BlockComp_One  = -2;
  static constexpr INT UNSUPPORTED    =  0;


  switch (fmt)
  {
    case DXGI_FORMAT_R32G32B32A32_TYPELESS:      return SixteenBytes;
    case DXGI_FORMAT_R32G32B32A32_FLOAT:         return SixteenBytes;
    case DXGI_FORMAT_R32G32B32A32_UINT:          return SixteenBytes;
    case DXGI_FORMAT_R32G32B32A32_SINT:          return SixteenBytes;

    case DXGI_FORMAT_R32G32B32_TYPELESS:         return TwelveBytes;
    case DXGI_FORMAT_R32G32B32_FLOAT:            return TwelveBytes;
    case DXGI_FORMAT_R32G32B32_UINT:             return TwelveBytes;
    case DXGI_FORMAT_R32G32B32_SINT:             return TwelveBytes;

    case DXGI_FORMAT_R16G16B16A16_TYPELESS:      return EightBytes;
    case DXGI_FORMAT_R16G16B16A16_FLOAT:         return EightBytes;
    case DXGI_FORMAT_R16G16B16A16_UNORM:         return EightBytes;
    case DXGI_FORMAT_R16G16B16A16_UINT:          return EightBytes;
    case DXGI_FORMAT_R16G16B16A16_SNORM:         return EightBytes;
    case DXGI_FORMAT_R16G16B16A16_SINT:          return EightBytes;

    case DXGI_FORMAT_R32G32_TYPELESS:            return EightBytes;
    case DXGI_FORMAT_R32G32_FLOAT:               return EightBytes;
    case DXGI_FORMAT_R32G32_UINT:                return EightBytes;
    case DXGI_FORMAT_R32G32_SINT:                return EightBytes;
    case DXGI_FORMAT_R32G8X24_TYPELESS:          return EightBytes;

    case DXGI_FORMAT_D32_FLOAT_S8X24_UINT:       return EightBytes;
    case DXGI_FORMAT_R32_FLOAT_X8X24_TYPELESS:   return EightBytes;
    case DXGI_FORMAT_X32_TYPELESS_G8X24_UINT:    return EightBytes;

    case DXGI_FORMAT_R10G10B10A2_TYPELESS:       return FourBytes;
    case DXGI_FORMAT_R10G10B10A2_UNORM:          return FourBytes;
    case DXGI_FORMAT_R10G10B10A2_UINT:           return FourBytes;
    case DXGI_FORMAT_R11G11B10_FLOAT:            return FourBytes;

    case DXGI_FORMAT_R8G8B8A8_TYPELESS:          return FourBytes;
    case DXGI_FORMAT_R8G8B8A8_UNORM:             return FourBytes;
    case DXGI_FORMAT_R8G8B8A8_UNORM_SRGB:        return FourBytes;
    case DXGI_FORMAT_R8G8B8A8_UINT:              return FourBytes;
    case DXGI_FORMAT_R8G8B8A8_SNORM:             return FourBytes;
    case DXGI_FORMAT_R8G8B8A8_SINT:              return FourBytes;

    case DXGI_FORMAT_R16G16_TYPELESS:            return FourBytes;
    case DXGI_FORMAT_R16G16_FLOAT:               return FourBytes;
    case DXGI_FORMAT_R16G16_UNORM:               return FourBytes;
    case DXGI_FORMAT_R16G16_UINT:                return FourBytes;
    case DXGI_FORMAT_R16G16_SNORM:               return FourBytes;
    case DXGI_FORMAT_R16G16_SINT:                return FourBytes;

    case DXGI_FORMAT_R32_TYPELESS:               return FourBytes;
    case DXGI_FORMAT_D32_FLOAT:                  return FourBytes;
    case DXGI_FORMAT_R32_FLOAT:                  return FourBytes;
    case DXGI_FORMAT_R32_UINT:                   return FourBytes;
    case DXGI_FORMAT_R32_SINT:                   return FourBytes;
    case DXGI_FORMAT_R24G8_TYPELESS:             return FourBytes;

    case DXGI_FORMAT_D24_UNORM_S8_UINT:          return FourBytes;
    case DXGI_FORMAT_R24_UNORM_X8_TYPELESS:      return FourBytes;
    case DXGI_FORMAT_X24_TYPELESS_G8_UINT:       return FourBytes;

    case DXGI_FORMAT_R8G8_TYPELESS:              return TwoBytes;
    case DXGI_FORMAT_R8G8_UNORM:                 return TwoBytes;
    case DXGI_FORMAT_R8G8_UINT:                  return TwoBytes;
    case DXGI_FORMAT_R8G8_SNORM:                 return TwoBytes;
    case DXGI_FORMAT_R8G8_SINT:                  return TwoBytes;

    case DXGI_FORMAT_R16_TYPELESS:               return TwoBytes;
    case DXGI_FORMAT_R16_FLOAT:                  return TwoBytes;
    case DXGI_FORMAT_D16_UNORM:                  return TwoBytes;
    case DXGI_FORMAT_R16_UNORM:                  return TwoBytes;
    case DXGI_FORMAT_R16_UINT:                   return TwoBytes;
    case DXGI_FORMAT_R16_SNORM:                  return TwoBytes;
    case DXGI_FORMAT_R16_SINT:                   return TwoBytes;

    case DXGI_FORMAT_R8_TYPELESS:                return OneByte;
    case DXGI_FORMAT_R8_UNORM:                   return OneByte;
    case DXGI_FORMAT_R8_UINT:                    return OneByte;
    case DXGI_FORMAT_R8_SNORM:                   return OneByte;
    case DXGI_FORMAT_R8_SINT:                    return OneByte;
    case DXGI_FORMAT_A8_UNORM:                   return OneByte;
    case DXGI_FORMAT_R1_UNORM:                   return OneByte;

    case DXGI_FORMAT_R9G9B9E5_SHAREDEXP:         return FourBytes;
    case DXGI_FORMAT_R8G8_B8G8_UNORM:            return FourBytes;
    case DXGI_FORMAT_G8R8_G8B8_UNORM:            return FourBytes;

    case DXGI_FORMAT_BC1_TYPELESS:               return BlockComp_Half;
    case DXGI_FORMAT_BC1_UNORM:                  return BlockComp_Half;
    case DXGI_FORMAT_BC1_UNORM_SRGB:             return BlockComp_Half;
    case DXGI_FORMAT_BC2_TYPELESS:               return BlockComp_One;
    case DXGI_FORMAT_BC2_UNORM:                  return BlockComp_One;
    case DXGI_FORMAT_BC2_UNORM_SRGB:             return BlockComp_One;
    case DXGI_FORMAT_BC3_TYPELESS:               return BlockComp_One;
    case DXGI_FORMAT_BC3_UNORM:                  return BlockComp_One;
    case DXGI_FORMAT_BC3_UNORM_SRGB:             return BlockComp_One;
    case DXGI_FORMAT_BC4_TYPELESS:               return BlockComp_Half;
    case DXGI_FORMAT_BC4_UNORM:                  return BlockComp_Half;
    case DXGI_FORMAT_BC4_SNORM:                  return BlockComp_Half;
    case DXGI_FORMAT_BC5_TYPELESS:               return BlockComp_One;
    case DXGI_FORMAT_BC5_UNORM:                  return BlockComp_One;
    case DXGI_FORMAT_BC5_SNORM:                  return BlockComp_One;

    case DXGI_FORMAT_B5G6R5_UNORM:               return TwoBytes;
    case DXGI_FORMAT_B5G5R5A1_UNORM:             return TwoBytes;
    case DXGI_FORMAT_B8G8R8X8_UNORM:             return FourBytes;
    case DXGI_FORMAT_B8G8R8A8_UNORM:             return FourBytes;
    case DXGI_FORMAT_R10G10B10_XR_BIAS_A2_UNORM: return FourBytes;
    case DXGI_FORMAT_B8G8R8A8_TYPELESS:          return FourBytes;
    case DXGI_FORMAT_B8G8R8A8_UNORM_SRGB:        return FourBytes;
    case DXGI_FORMAT_B8G8R8X8_TYPELESS:          return FourBytes;
    case DXGI_FORMAT_B8G8R8X8_UNORM_SRGB:        return FourBytes;

    case DXGI_FORMAT_BC6H_TYPELESS:              return BlockComp_One;
    case DXGI_FORMAT_BC6H_UF16:                  return BlockComp_One;
    case DXGI_FORMAT_BC6H_SF16:                  return BlockComp_One;
    case DXGI_FORMAT_BC7_TYPELESS:               return BlockComp_One;
    case DXGI_FORMAT_BC7_UNORM:                  return BlockComp_One;
    case DXGI_FORMAT_BC7_UNORM_SRGB:             return BlockComp_One;

    case DXGI_FORMAT_AYUV:                       SK_ReleaseAssert (!"Bad Format"); return UNSUPPORTED;
    case DXGI_FORMAT_Y410:                       SK_ReleaseAssert (!"Bad Format"); return UNSUPPORTED;
    case DXGI_FORMAT_Y416:                       SK_ReleaseAssert (!"Bad Format"); return UNSUPPORTED;
    case DXGI_FORMAT_NV12:                       SK_ReleaseAssert (!"Bad Format"); return UNSUPPORTED;
    case DXGI_FORMAT_P010:                       SK_ReleaseAssert (!"Bad Format"); return UNSUPPORTED;
    case DXGI_FORMAT_P016:                       SK_ReleaseAssert (!"Bad Format"); return UNSUPPORTED;
    case DXGI_FORMAT_420_OPAQUE:                 SK_ReleaseAssert (!"Bad Format"); return UNSUPPORTED;
    case DXGI_FORMAT_YUY2:                       SK_ReleaseAssert (!"Bad Format"); return UNSUPPORTED;
    case DXGI_FORMAT_Y210:                       SK_ReleaseAssert (!"Bad Format"); return UNSUPPORTED;
    case DXGI_FORMAT_Y216:                       SK_ReleaseAssert (!"Bad Format"); return UNSUPPORTED;
    case DXGI_FORMAT_NV11:                       SK_ReleaseAssert (!"Bad Format"); return UNSUPPORTED;
    case DXGI_FORMAT_AI44:                       SK_ReleaseAssert (!"Bad Format"); return UNSUPPORTED;
    case DXGI_FORMAT_IA44:                       SK_ReleaseAssert (!"Bad Format"); return UNSUPPORTED;
    case DXGI_FORMAT_P8:                         return OneByte;
    case DXGI_FORMAT_A8P8:                       return TwoBytes;
    case DXGI_FORMAT_B4G4R4A4_UNORM:             return TwoBytes;

    default: SK_ReleaseAssert (!"Unknown Format");
                                                 return UNSUPPORTED;
  }
}
std::wstring
__stdcall
SK_DXGI_FormatToStr (DXGI_FORMAT fmt) noexcept
{
  switch (fmt)
  {
    case DXGI_FORMAT_R32G32B32A32_TYPELESS:      return L"DXGI_FORMAT_R32G32B32A32_TYPELESS";
    case DXGI_FORMAT_R32G32B32A32_FLOAT:         return L"DXGI_FORMAT_R32G32B32A32_FLOAT";
    case DXGI_FORMAT_R32G32B32A32_UINT:          return L"DXGI_FORMAT_R32G32B32A32_UINT";
    case DXGI_FORMAT_R32G32B32A32_SINT:          return L"DXGI_FORMAT_R32G32B32A32_SINT";

    case DXGI_FORMAT_R32G32B32_TYPELESS:         return L"DXGI_FORMAT_R32G32B32_TYPELESS";
    case DXGI_FORMAT_R32G32B32_FLOAT:            return L"DXGI_FORMAT_R32G32B32_FLOAT";
    case DXGI_FORMAT_R32G32B32_UINT:             return L"DXGI_FORMAT_R32G32B32_UINT";
    case DXGI_FORMAT_R32G32B32_SINT:             return L"DXGI_FORMAT_R32G32B32_SINT";

    case DXGI_FORMAT_R16G16B16A16_TYPELESS:      return L"DXGI_FORMAT_R16G16B16A16_TYPELESS";
    case DXGI_FORMAT_R16G16B16A16_FLOAT:         return L"DXGI_FORMAT_R16G16B16A16_FLOAT";
    case DXGI_FORMAT_R16G16B16A16_UNORM:         return L"DXGI_FORMAT_R16G16B16A16_UNORM";
    case DXGI_FORMAT_R16G16B16A16_UINT:          return L"DXGI_FORMAT_R16G16B16A16_UINT";
    case DXGI_FORMAT_R16G16B16A16_SNORM:         return L"DXGI_FORMAT_R16G16B16A16_SNORM";
    case DXGI_FORMAT_R16G16B16A16_SINT:          return L"DXGI_FORMAT_R16G16B16A16_SINT";

    case DXGI_FORMAT_R32G32_TYPELESS:            return L"DXGI_FORMAT_R32G32_TYPELESS";
    case DXGI_FORMAT_R32G32_FLOAT:               return L"DXGI_FORMAT_R32G32_FLOAT";
    case DXGI_FORMAT_R32G32_UINT:                return L"DXGI_FORMAT_R32G32_UINT";
    case DXGI_FORMAT_R32G32_SINT:                return L"DXGI_FORMAT_R32G32_SINT";
    case DXGI_FORMAT_R32G8X24_TYPELESS:          return L"DXGI_FORMAT_R32G8X24_TYPELESS";

    case DXGI_FORMAT_D32_FLOAT_S8X24_UINT:       return L"DXGI_FORMAT_D32_FLOAT_S8X24_UINT";
    case DXGI_FORMAT_R32_FLOAT_X8X24_TYPELESS:   return L"DXGI_FORMAT_R32_FLOAT_X8X24_TYPELESS";
    case DXGI_FORMAT_X32_TYPELESS_G8X24_UINT:    return L"DXGI_FORMAT_X32_TYPELESS_G8X24_UINT";

    case DXGI_FORMAT_R10G10B10A2_TYPELESS:       return L"DXGI_FORMAT_R10G10B10A2_TYPELESS";
    case DXGI_FORMAT_R10G10B10A2_UNORM:          return L"DXGI_FORMAT_R10G10B10A2_UNORM";
    case DXGI_FORMAT_R10G10B10A2_UINT:           return L"DXGI_FORMAT_R10G10B10A2_UINT";
    case DXGI_FORMAT_R11G11B10_FLOAT:            return L"DXGI_FORMAT_R11G11B10_FLOAT";

    case DXGI_FORMAT_R8G8B8A8_TYPELESS:          return L"DXGI_FORMAT_R8G8B8A8_TYPELESS";
    case DXGI_FORMAT_R8G8B8A8_UNORM:             return L"DXGI_FORMAT_R8G8B8A8_UNORM";
    case DXGI_FORMAT_R8G8B8A8_UNORM_SRGB:        return L"DXGI_FORMAT_R8G8B8A8_UNORM_SRGB";
    case DXGI_FORMAT_R8G8B8A8_UINT:              return L"DXGI_FORMAT_R8G8B8A8_UINT";
    case DXGI_FORMAT_R8G8B8A8_SNORM:             return L"DXGI_FORMAT_R8G8B8A8_SNORM";
    case DXGI_FORMAT_R8G8B8A8_SINT:              return L"DXGI_FORMAT_R8G8B8A8_SINT";

    case DXGI_FORMAT_R16G16_TYPELESS:            return L"DXGI_FORMAT_R16G16_TYPELESS";
    case DXGI_FORMAT_R16G16_FLOAT:               return L"DXGI_FORMAT_R16G16_FLOAT";
    case DXGI_FORMAT_R16G16_UNORM:               return L"DXGI_FORMAT_R16G16_UNORM";
    case DXGI_FORMAT_R16G16_UINT:                return L"DXGI_FORMAT_R16G16_UINT";
    case DXGI_FORMAT_R16G16_SNORM:               return L"DXGI_FORMAT_R16G16_SNORM";
    case DXGI_FORMAT_R16G16_SINT:                return L"DXGI_FORMAT_R16G16_SINT";

    case DXGI_FORMAT_R32_TYPELESS:               return L"DXGI_FORMAT_R32_TYPELESS";
    case DXGI_FORMAT_D32_FLOAT:                  return L"DXGI_FORMAT_D32_FLOAT";
    case DXGI_FORMAT_R32_FLOAT:                  return L"DXGI_FORMAT_R32_FLOAT";
    case DXGI_FORMAT_R32_UINT:                   return L"DXGI_FORMAT_R32_UINT";
    case DXGI_FORMAT_R32_SINT:                   return L"DXGI_FORMAT_R32_SINT";
    case DXGI_FORMAT_R24G8_TYPELESS:             return L"DXGI_FORMAT_R24G8_TYPELESS";

    case DXGI_FORMAT_D24_UNORM_S8_UINT:          return L"DXGI_FORMAT_D24_UNORM_S8_UINT";
    case DXGI_FORMAT_R24_UNORM_X8_TYPELESS:      return L"DXGI_FORMAT_R24_UNORM_X8_TYPELESS";
    case DXGI_FORMAT_X24_TYPELESS_G8_UINT:       return L"DXGI_FORMAT_X24_TYPELESS_G8_UINT";

    case DXGI_FORMAT_R8G8_TYPELESS:              return L"DXGI_FORMAT_R8G8_TYPELESS";
    case DXGI_FORMAT_R8G8_UNORM:                 return L"DXGI_FORMAT_R8G8_UNORM";
    case DXGI_FORMAT_R8G8_UINT:                  return L"DXGI_FORMAT_R8G8_UINT";
    case DXGI_FORMAT_R8G8_SNORM:                 return L"DXGI_FORMAT_R8G8_SNORM";
    case DXGI_FORMAT_R8G8_SINT:                  return L"DXGI_FORMAT_R8G8_SINT";

    case DXGI_FORMAT_R16_TYPELESS:               return L"DXGI_FORMAT_R16_TYPELESS";
    case DXGI_FORMAT_R16_FLOAT:                  return L"DXGI_FORMAT_R16_FLOAT";
    case DXGI_FORMAT_D16_UNORM:                  return L"DXGI_FORMAT_D16_UNORM";
    case DXGI_FORMAT_R16_UNORM:                  return L"DXGI_FORMAT_R16_UNORM";
    case DXGI_FORMAT_R16_UINT:                   return L"DXGI_FORMAT_R16_UINT";
    case DXGI_FORMAT_R16_SNORM:                  return L"DXGI_FORMAT_R16_SNORM";
    case DXGI_FORMAT_R16_SINT:                   return L"DXGI_FORMAT_R16_SINT";

    case DXGI_FORMAT_R8_TYPELESS:                return L"DXGI_FORMAT_R8_TYPELESS";
    case DXGI_FORMAT_R8_UNORM:                   return L"DXGI_FORMAT_R8_UNORM";
    case DXGI_FORMAT_R8_UINT:                    return L"DXGI_FORMAT_R8_UINT";
    case DXGI_FORMAT_R8_SNORM:                   return L"DXGI_FORMAT_R8_SNORM";
    case DXGI_FORMAT_R8_SINT:                    return L"DXGI_FORMAT_R8_SINT";
    case DXGI_FORMAT_A8_UNORM:                   return L"DXGI_FORMAT_A8_UNORM";
    case DXGI_FORMAT_R1_UNORM:                   return L"DXGI_FORMAT_R1_UNORM";

    case DXGI_FORMAT_R9G9B9E5_SHAREDEXP:         return L"DXGI_FORMAT_R9G9B9E5_SHAREDEXP";
    case DXGI_FORMAT_R8G8_B8G8_UNORM:            return L"DXGI_FORMAT_R8G8_B8G8_UNORM";
    case DXGI_FORMAT_G8R8_G8B8_UNORM:            return L"DXGI_FORMAT_G8R8_G8B8_UNORM";

    case DXGI_FORMAT_BC1_TYPELESS:               return L"DXGI_FORMAT_BC1_TYPELESS";
    case DXGI_FORMAT_BC1_UNORM:                  return L"DXGI_FORMAT_BC1_UNORM";
    case DXGI_FORMAT_BC1_UNORM_SRGB:             return L"DXGI_FORMAT_BC1_UNORM_SRGB";
    case DXGI_FORMAT_BC2_TYPELESS:               return L"DXGI_FORMAT_BC2_TYPELESS";
    case DXGI_FORMAT_BC2_UNORM:                  return L"DXGI_FORMAT_BC2_UNORM";
    case DXGI_FORMAT_BC2_UNORM_SRGB:             return L"DXGI_FORMAT_BC2_UNORM_SRGB";
    case DXGI_FORMAT_BC3_TYPELESS:               return L"DXGI_FORMAT_BC3_TYPELESS";
    case DXGI_FORMAT_BC3_UNORM:                  return L"DXGI_FORMAT_BC3_UNORM";
    case DXGI_FORMAT_BC3_UNORM_SRGB:             return L"DXGI_FORMAT_BC3_UNORM_SRGB";
    case DXGI_FORMAT_BC4_TYPELESS:               return L"DXGI_FORMAT_BC4_TYPELESS";
    case DXGI_FORMAT_BC4_UNORM:                  return L"DXGI_FORMAT_BC4_UNORM";
    case DXGI_FORMAT_BC4_SNORM:                  return L"DXGI_FORMAT_BC4_SNORM";
    case DXGI_FORMAT_BC5_TYPELESS:               return L"DXGI_FORMAT_BC5_TYPELESS";
    case DXGI_FORMAT_BC5_UNORM:                  return L"DXGI_FORMAT_BC5_UNORM";
    case DXGI_FORMAT_BC5_SNORM:                  return L"DXGI_FORMAT_BC5_SNORM";

    case DXGI_FORMAT_B5G6R5_UNORM:               return L"DXGI_FORMAT_B5G6R5_UNORM";
    case DXGI_FORMAT_B5G5R5A1_UNORM:             return L"DXGI_FORMAT_B5G5R5A1_UNORM";
    case DXGI_FORMAT_B8G8R8X8_UNORM:             return L"DXGI_FORMAT_B8G8R8X8_UNORM";
    case DXGI_FORMAT_B8G8R8A8_UNORM:             return L"DXGI_FORMAT_B8G8R8A8_UNORM";
    case DXGI_FORMAT_R10G10B10_XR_BIAS_A2_UNORM: return L"DXGI_FORMAT_R10G10B10_XR_BIAS_A2_UNORM";
    case DXGI_FORMAT_B8G8R8A8_TYPELESS:          return L"DXGI_FORMAT_B8G8R8A8_TYPELESS";
    case DXGI_FORMAT_B8G8R8A8_UNORM_SRGB:        return L"DXGI_FORMAT_B8G8R8A8_UNORM_SRGB";
    case DXGI_FORMAT_B8G8R8X8_TYPELESS:          return L"DXGI_FORMAT_B8G8R8X8_TYPELESS";
    case DXGI_FORMAT_B8G8R8X8_UNORM_SRGB:        return L"DXGI_FORMAT_B8G8R8X8_UNORM_SRGB";

    case DXGI_FORMAT_BC6H_TYPELESS:              return L"DXGI_FORMAT_BC6H_TYPELESS";
    case DXGI_FORMAT_BC6H_UF16:                  return L"DXGI_FORMAT_BC6H_UF16";
    case DXGI_FORMAT_BC6H_SF16:                  return L"DXGI_FORMAT_BC6H_SF16";
    case DXGI_FORMAT_BC7_TYPELESS:               return L"DXGI_FORMAT_BC7_TYPELESS";
    case DXGI_FORMAT_BC7_UNORM:                  return L"DXGI_FORMAT_BC7_UNORM";
    case DXGI_FORMAT_BC7_UNORM_SRGB:             return L"DXGI_FORMAT_BC7_UNORM_SRGB";

    case DXGI_FORMAT_AYUV:                       return L"DXGI_FORMAT_AYUV";
    case DXGI_FORMAT_Y410:                       return L"DXGI_FORMAT_Y410";
    case DXGI_FORMAT_Y416:                       return L"DXGI_FORMAT_Y416";
    case DXGI_FORMAT_NV12:                       return L"DXGI_FORMAT_NV12";
    case DXGI_FORMAT_P010:                       return L"DXGI_FORMAT_P010";
    case DXGI_FORMAT_P016:                       return L"DXGI_FORMAT_P016";
    case DXGI_FORMAT_420_OPAQUE:                 return L"DXGI_FORMAT_420_OPAQUE";
    case DXGI_FORMAT_YUY2:                       return L"DXGI_FORMAT_YUY2";
    case DXGI_FORMAT_Y210:                       return L"DXGI_FORMAT_Y210";
    case DXGI_FORMAT_Y216:                       return L"DXGI_FORMAT_Y216";
    case DXGI_FORMAT_NV11:                       return L"DXGI_FORMAT_NV11";
    case DXGI_FORMAT_AI44:                       return L"DXGI_FORMAT_AI44";
    case DXGI_FORMAT_IA44:                       return L"DXGI_FORMAT_IA44";
    case DXGI_FORMAT_P8:                         return L"DXGI_FORMAT_P8";
    case DXGI_FORMAT_A8P8:                       return L"DXGI_FORMAT_A8P8";
    case DXGI_FORMAT_B4G4R4A4_UNORM:             return L"DXGI_FORMAT_B4G4R4A4_UNORM";

    default:                                     return L"UNKNOWN";
  }
}

#include <shaders/sRGB_codec_ps.h>
#include <shaders/vs_colorutil.h>

struct SK_DXGI_sRGBCoDec {
  struct params_cbuffer_s {
    BOOL passthrough;
    BOOL apply;
    BOOL strip;
    BOOL padding;
  } params;

  ID3D11InputLayout*            pInputLayout = nullptr;
  ID3D11Buffer*                  pSRGBParams = nullptr;

  ID3D11SamplerState*              pSampler  = nullptr;

  ID3D11ShaderResourceView*   pBackbufferSrv = nullptr;
  ID3D11RenderTargetView*     pBackbufferRtv = nullptr;

  ID3D11RasterizerState*        pRasterState = nullptr;
  ID3D11DepthStencilState*          pDSState = nullptr;

  ID3D11BlendState*             pBlendState  = nullptr;

  SK_D3D11_Stateblock_Lite*               sb = nullptr;

  SK::DXGI::ShaderBase <ID3D11PixelShader>  PixelShader_sRGB_NoMore;
  SK::DXGI::ShaderBase <ID3D11VertexShader> VertexShader_Util;

  void releaseResources (void)
  {
    if (pSampler       != nullptr) { pSampler->Release       (); pSampler       = nullptr; }

    if (pBackbufferSrv != nullptr) { pBackbufferSrv->Release (); pBackbufferSrv = nullptr; }
    if (pBackbufferRtv != nullptr) { pBackbufferRtv->Release (); pBackbufferRtv = nullptr; }

    if (pRasterState   != nullptr) { pRasterState->Release   (); pRasterState   = nullptr; }
    if (pDSState       != nullptr) { pDSState->Release       ();     pDSState   = nullptr; }

    if (pSRGBParams    != nullptr) { pSRGBParams->Release    (); pSRGBParams    = nullptr; }
    if (pBlendState    != nullptr) { pBlendState->Release    (); pBlendState    = nullptr; }
    if (pInputLayout   != nullptr) { pInputLayout->Release   (); pInputLayout   = nullptr; }

    PixelShader_sRGB_NoMore.releaseResources ();
    VertexShader_Util.releaseResources       ();
  }

  bool
  recompileShaders (void)
  {
    std::wstring debug_shader_dir = SK_GetConfigPath ();
                 debug_shader_dir +=
            LR"(SK_Res\Debug\shaders\)";

    static auto& rb =
      SK_GetCurrentRenderBackend ();

    SK_ComQIPtr <ID3D11Device> pDev (rb.device);

    bool ret =
      pDev->CreatePixelShader ( sRGB_codec_ps_bytecode,
                        sizeof (sRGB_codec_ps_bytecode),
             nullptr, &PixelShader_sRGB_NoMore.shader ) == S_OK;
    ret &=
      pDev->CreateVertexShader ( colorutil_vs_bytecode,
                         sizeof (colorutil_vs_bytecode),
            nullptr, &VertexShader_Util.shader ) == S_OK;

    return ret;
  }

  void
  reloadResources (void)
  {
    releaseResources ();

    static auto& rb =
      SK_GetCurrentRenderBackend ();

    SK_ComQIPtr <IDXGISwapChain>      pSwapChain (rb.swapchain);
    SK_ComQIPtr <ID3D11Device>        pDev       (rb.device);
    SK_ComQIPtr <ID3D11DeviceContext> pDevCtx    (rb.d3d11.immediate_ctx);

    if (pDev != nullptr)
    {
      if (! recompileShaders ())
        return;

      if ( pBackbufferSrv == nullptr &&
               pSwapChain != nullptr )
      {
        DXGI_SWAP_CHAIN_DESC swapDesc = { };
        D3D11_TEXTURE2D_DESC desc     = { };

        pSwapChain->GetDesc (&swapDesc);

        desc.Width              = swapDesc.BufferDesc.Width;
        desc.Height             = swapDesc.BufferDesc.Height;
        desc.MipLevels          = 1;
        desc.ArraySize          = 1;
        desc.Format             = swapDesc.BufferDesc.Format;
        desc.SampleDesc.Count   = 1;
        desc.SampleDesc.Quality = 0;
        desc.Usage              = D3D11_USAGE_DEFAULT;
        desc.BindFlags          = D3D11_BIND_SHADER_RESOURCE;
        desc.CPUAccessFlags     = 0x0;
        desc.MiscFlags          = 0x0;

        SK_ComPtr <ID3D11Texture2D> pSRGBTexture;

        pDev->CreateTexture2D          (&desc,        nullptr, &pSRGBTexture);
        pDev->CreateShaderResourceView (pSRGBTexture, nullptr, &pBackbufferSrv);

        D3D11_INPUT_ELEMENT_DESC local_layout [] = {
          { "", 0, DXGI_FORMAT_R32_UINT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0 }
        };

        pDev->CreateInputLayout ( local_layout, 1,
                                    (DWORD *)(colorutil_vs_bytecode),
                                      sizeof (colorutil_vs_bytecode) /
                                      sizeof (colorutil_vs_bytecode [0]),
                                        &pInputLayout );

        D3D11_SAMPLER_DESC
          sampler_desc                    = { };

          sampler_desc.Filter             = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
          sampler_desc.AddressU           = D3D11_TEXTURE_ADDRESS_WRAP;
          sampler_desc.AddressV           = D3D11_TEXTURE_ADDRESS_WRAP;
          sampler_desc.AddressW           = D3D11_TEXTURE_ADDRESS_WRAP;
          sampler_desc.MipLODBias         = 0.f;
          sampler_desc.MaxAnisotropy      =   1;
          sampler_desc.ComparisonFunc     =  D3D11_COMPARISON_NEVER;
          sampler_desc.MinLOD             = -D3D11_FLOAT32_MAX;
          sampler_desc.MaxLOD             =  D3D11_FLOAT32_MAX;

        pDev->CreateSamplerState ( &sampler_desc,
                                              &pSampler );

        D3D11_BLEND_DESC
        blend_desc                                        = { };
        blend_desc.AlphaToCoverageEnable                  = false;
        blend_desc.RenderTarget [0].BlendEnable           = false;
        blend_desc.RenderTarget [0].SrcBlend              = D3D11_BLEND_ONE;
        blend_desc.RenderTarget [0].DestBlend             = D3D11_BLEND_ZERO;
        blend_desc.RenderTarget [0].BlendOp               = D3D11_BLEND_OP_ADD;
        blend_desc.RenderTarget [0].SrcBlendAlpha         = D3D11_BLEND_ONE;
        blend_desc.RenderTarget [0].DestBlendAlpha        = D3D11_BLEND_ZERO;
        blend_desc.RenderTarget [0].BlendOpAlpha          = D3D11_BLEND_OP_ADD;
        blend_desc.RenderTarget [0].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;

        pDev->CreateBlendState ( &blend_desc,
                                   &pBlendState );
      }

      D3D11_BUFFER_DESC desc = { };

      desc.ByteWidth         = sizeof (params_cbuffer_s);
      desc.Usage             = D3D11_USAGE_DYNAMIC;
      desc.BindFlags         = D3D11_BIND_CONSTANT_BUFFER;
      desc.CPUAccessFlags    = D3D11_CPU_ACCESS_WRITE;
      desc.MiscFlags         = 0;

      pDev->CreateBuffer (&desc, nullptr, &pSRGBParams);
    }
  }
};

SK_LazyGlobal <SK_DXGI_sRGBCoDec> srgb_codec;

void
SK_D3D11_CaptureStateBlock ( ID3D11DeviceContext*       pImmediateContext,
                             SK_D3D11_Stateblock_Lite** pSB );
void
SK_D3D11_ApplyStateBlock ( SK_D3D11_Stateblock_Lite* pBlock,
                           ID3D11DeviceContext*      pDevCtx );

void
SK_DXGI_ReleaseSRGBLinearizer (void)
{
  srgb_codec->releaseResources ();
}

bool
SK_DXGI_LinearizeSRGB (IDXGISwapChain* pChainThatUsedToBeSRGB)
{
  static auto& rb =
    SK_GetCurrentRenderBackend ();

  SK_ComQIPtr <IDXGISwapChain>      pSwapChain (pChainThatUsedToBeSRGB);// rb.swapchain);
  SK_ComQIPtr <ID3D11Device>        pDev       (rb.device);
  SK_ComQIPtr <ID3D11DeviceContext> pDevCtx    (rb.d3d11.immediate_ctx);

  if (! pDevCtx)
    return false;

  auto& vs_hdr_util  = srgb_codec->VertexShader_Util;
  auto& ps_hdr_scrgb = srgb_codec->PixelShader_sRGB_NoMore;

  if ( vs_hdr_util.shader  == nullptr ||
       ps_hdr_scrgb.shader == nullptr    )
  {
    srgb_codec->reloadResources ();
  }

  SK_ComPtr <ID3D11Resource> pSrc = nullptr;
  SK_ComPtr <ID3D11Resource> pDst = nullptr;
  DXGI_SWAP_CHAIN_DESC   swapDesc = { };

  if (pSwapChain != nullptr)
  {
    pSwapChain->GetDesc (&swapDesc);

    if ( SUCCEEDED (
           pSwapChain->GetBuffer    (
             0,
               IID_ID3D11Texture2D,
                 (void **)&pSrc.p  )
                   )
       )
    {
      srgb_codec->pBackbufferSrv->GetResource ( &pDst.p );
                        pDevCtx->CopyResource (  pDst, pSrc );
    }
  }

  SK_D3D11_CaptureStateBlock (pDevCtx, &srgb_codec->sb);

  D3D11_MAPPED_SUBRESOURCE mapped_resource  = { };

  if ( SUCCEEDED (
         pDevCtx->Map ( srgb_codec->pSRGBParams,
                          0, D3D11_MAP_WRITE_DISCARD, 0,
                             &mapped_resource )
                 )
     )
  {
    srgb_codec->params.passthrough = config.render.dxgi.srgb_behavior == -1;
    srgb_codec->params.apply       = config.render.dxgi.srgb_behavior ==  1;
    srgb_codec->params.strip       = config.render.dxgi.srgb_behavior ==  0;

    _ReadWriteBarrier ();

    memcpy (          static_cast <SK_DXGI_sRGBCoDec::params_cbuffer_s *> (mapped_resource.pData),
       &srgb_codec->params, sizeof SK_DXGI_sRGBCoDec::params_cbuffer_s );

    pDevCtx->Unmap (srgb_codec->pSRGBParams, 0);
  }

  D3D11_PRIMITIVE_TOPOLOGY       OrigPrimTop;
  SK_ComPtr <ID3D11VertexShader> pVS_Orig;
  SK_ComPtr <ID3D11PixelShader>  pPS_Orig;
  SK_ComPtr <ID3D11BlendState>   pBlendState_Orig;
                            UINT uiOrigBlendMask;
                           FLOAT fOrigBlendFactors [4] = { };
  SK_ComPtr <ID3D11Buffer>       pConstantBufferPS_Orig;

  pDevCtx->PSGetConstantBuffers (0,                                   1, &pConstantBufferPS_Orig.p);
  pDevCtx->OMGetBlendState      (&pBlendState_Orig.p, fOrigBlendFactors,          &uiOrigBlendMask);
  pDevCtx->VSGetShader          (&pVS_Orig.p,                   nullptr,                   nullptr);
  pDevCtx->PSGetShader          (&pPS_Orig.p,                   nullptr,                   nullptr);

  SK_ComPtr <ID3D11ShaderResourceView> pOrigResources [1] = {                            };
  SK_ComPtr <ID3D11ShaderResourceView> pResources     [1] = { srgb_codec->pBackbufferSrv };

  D3D11_RENDER_TARGET_VIEW_DESC rtdesc               = {                           };
                                rtdesc.ViewDimension = D3D11_RTV_DIMENSION_TEXTURE2D;
                                rtdesc.Format        = DirectX::MakeSRGB (swapDesc.BufferDesc.Format);

  pDevCtx->PSGetShaderResources   (0, 1, &pOrigResources [0].p);

  pDevCtx->IAGetPrimitiveTopology (&OrigPrimTop);
  pDevCtx->IASetPrimitiveTopology (D3D11_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP);
  pDevCtx->IASetVertexBuffers     (0, 0, nullptr, nullptr, nullptr);
  pDevCtx->IASetInputLayout       (srgb_codec->pInputLayout);
  pDevCtx->IASetIndexBuffer       (nullptr, DXGI_FORMAT_UNKNOWN, 0);

  static const FLOAT                      fBlendFactor [4] =
                                    { 0.0f, 0.0f, 0.0f, 0.0f };
  pDevCtx->OMSetBlendState      (srgb_codec->pBlendState,
                                          fBlendFactor,           0xFFFFFFFF);

  pDevCtx->VSSetShader          (srgb_codec->VertexShader_Util.shader,       nullptr, 0);
  pDevCtx->PSSetShader          (srgb_codec->PixelShader_sRGB_NoMore.shader, nullptr, 0);
  pDevCtx->PSSetConstantBuffers (0,            1,     &srgb_codec->pSRGBParams);

  SK_ComPtr <ID3D11RenderTargetView> pRenderTargetView = nullptr;

  if ( SUCCEEDED (
         pDev->CreateRenderTargetView ( pSrc, &rtdesc,
                                  &pRenderTargetView.p )
       )
     )
  {
    pDevCtx->RSSetScissorRects        (0, nullptr);
    pDevCtx->RSSetState               (nullptr   );
    pDevCtx->OMSetDepthStencilState   (nullptr, 0);

    pDevCtx->OMSetRenderTargets  ( 1,
                                    &pRenderTargetView.p,
                                       nullptr );

    pDevCtx->PSSetShaderResources (0,                         1,          &pResources [0].p);
    pDevCtx->PSSetSamplers        (0, 1, &srgb_codec->pSampler);

    pDevCtx->HSSetShader  (nullptr, nullptr, 0);
    pDevCtx->DSSetShader  (nullptr, nullptr, 0);
    pDevCtx->GSSetShader  (nullptr, nullptr, 0);
    pDevCtx->SOSetTargets (0, nullptr, nullptr);

    pDevCtx->Draw (4, 0);

    pDevCtx->PSSetShaderResources   (0,                       1,          &pOrigResources [0].p);
    pDevCtx->IASetPrimitiveTopology (OrigPrimTop);
    pDevCtx->PSSetShader            (pPS_Orig,         nullptr,           0);
    pDevCtx->VSSetShader            (pVS_Orig,         nullptr,           0);
    pDevCtx->PSSetConstantBuffers   (0,                       1,          &pConstantBufferPS_Orig.p);
    pDevCtx->OMSetBlendState        (pBlendState_Orig, fOrigBlendFactors, uiOrigBlendMask);
  }

  SK_D3D11_ApplyStateBlock (srgb_codec->sb, pDevCtx);

  return true;
}