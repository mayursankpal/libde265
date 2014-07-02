/*
 * H.265 video codec.
 * Copyright (c) 2013-2014 struktur AG, Dirk Farin <farin@struktur.de>
 *
 * This file is part of libde265.
 *
 * libde265 is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * libde265 is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with libde265.  If not, see <http://www.gnu.org/licenses/>.
 */


#include "libde265/nal-parser.h"
#include "libde265/decctx.h"
#include "libde265/encode.h"
#include "libde265/slice.h"
#include "libde265/scan.h"
#include "libde265/intrapred.h"
#include "libde265/fallback-dct.h"
#include <assert.h>

error_queue errqueue;

video_parameter_set vps;
seq_parameter_set   sps;
pic_parameter_set   pps;
slice_segment_header shdr;

CABAC_encoder writer;

de265_image img;
encoder_context ectx;


void encode_image_coeffTest_1()
{
  int w = sps.pic_width_in_luma_samples;
  int h = sps.pic_height_in_luma_samples;

  img.alloc_image(w,h, de265_chroma_420, &sps, true, NULL /* no decctx */);
  img.alloc_encoder_data(&sps);

  initialize_CABAC_models(ectx.ctx_model, shdr.initType, shdr.SliceQPY);

  int Log2CtbSize = sps.Log2CtbSizeY;


  // encode CTB by CTB

  for (int y=0;y<sps.PicHeightInCtbsY;y++)
    for (int x=0;x<sps.PicWidthInCtbsY;x++)
      {
        enc_cb* cb = ectx.enc_cb_pool.get_new();
        cb->split_cu_flag = false;

        cb->cu_transquant_bypass_flag = false;
        cb->PredMode = MODE_INTRA;
        cb->PartMode = PART_2Nx2N;

        enc_pb_intra* pb = ectx.enc_pb_intra_pool.get_new();
        cb->intra_pb[0] = pb;
        pb->pred_mode = INTRA_DC;
        pb->pred_mode_chroma = INTRA_DC;


        enc_tb* tb = ectx.enc_tb_pool.get_new();
        cb->transform_tree = tb;

        tb->parent = NULL;
        tb->split_transform_flag = false;
        tb->cbf_luma = true;
        tb->cbf_cb   = false;
        tb->cbf_cr   = false;

        int16_t coeff[16*16];
        memset(coeff,0,16*16*sizeof(int16_t));
        coeff[0] = 1; //((x+y)/*&1*/) * 5 - 101;
        //coeff[2+1*16] = 25;
        //coeff[3+4*16] = 30;
        if (x%2==0 && y%2==0)
          coeff[((x/2)%16) + ((y/2)%16)*16] = 100;

        //if (x+y==0) coeff[0]=-80;
        tb->coeff[0] = coeff;

        cb->write_to_image(&img, x<<Log2CtbSize, y<<Log2CtbSize, Log2CtbSize, true);
        encode_ctb(&ectx, cb, x,y);

        int last = (y==sps.PicHeightInCtbsY-1 &&
                    x==sps.PicWidthInCtbsY-1);

        printf("wrote CTB at %d;%d\n",x*16,y*16);
        printf("write term bit: %d\n",last);
        writer.write_CABAC_term_bit(last);


        ectx.enc_cb_pool.free_all();
        ectx.enc_tb_pool.free_all();
        ectx.enc_pb_intra_pool.free_all();
      }
}


void encode_image_FDCT_1()
{
  FILE* fh = fopen("paris_cif.yuv","rb");
  uint8_t* input[3];
  input[0] = (uint8_t*)malloc(352*288);
  input[1] = (uint8_t*)malloc(352*288/4);
  input[2] = (uint8_t*)malloc(352*288/4);
  fread(input[0],352,288,fh);
  fread(input[1],352,288/4,fh);
  fread(input[2],352,288/4,fh);
  fclose(fh);
  int stride = 352;

  int w = sps.pic_width_in_luma_samples;
  int h = sps.pic_height_in_luma_samples;

  img.alloc_image(w,h, de265_chroma_420, &sps, true, NULL /* no decctx */);
  img.alloc_encoder_data(&sps);

  initialize_CABAC_models(ectx.ctx_model, shdr.initType, shdr.SliceQPY);

  int Log2CtbSize = sps.Log2CtbSizeY;


  // encode CTB by CTB

  for (int y=0;y<sps.PicHeightInCtbsY;y++)
    for (int x=0;x<sps.PicWidthInCtbsY;x++)
      {
        enc_cb* cb = ectx.enc_cb_pool.get_new();
        cb->split_cu_flag = false;

        cb->cu_transquant_bypass_flag = false;
        cb->PredMode = MODE_INTRA;
        cb->PartMode = PART_2Nx2N;

        enc_pb_intra* pb = ectx.enc_pb_intra_pool.get_new();
        cb->intra_pb[0] = pb;
        pb->pred_mode = INTRA_DC;
        pb->pred_mode_chroma = INTRA_DC;


        enc_tb* tb = ectx.enc_tb_pool.get_new();
        cb->transform_tree = tb;

        tb->parent = NULL;
        tb->split_transform_flag = false;
        tb->cbf_luma = true;
        tb->cbf_cb   = true;
        tb->cbf_cr   = true;

        int16_t coeff_luma[16*16], coeff_cb[8*8], coeff_cr[8*8];
        /*
        fdct_16x16_8_fallback(coeff_luma, &input[0][(y<<Log2CtbSize)*stride + (x<<Log2CtbSize)], stride);
        fdct_8x8_8_fallback(coeff_cb, &input[1][(y<<Log2CtbSize)*stride/4 + (x<<Log2CtbSize)/2], stride/2);
        fdct_8x8_8_fallback(coeff_cr, &input[2][(y<<Log2CtbSize)*stride/4 + (x<<Log2CtbSize)/2], stride/2);
        */
        coeff_luma[0]=-1;
        coeff_cb[0] -= 16;
        coeff_cr[0] -= 16;
        if (coeff_cb[0]==0) coeff_cb[0]=((x+y)&1 ? -1 : 1);
        if (coeff_cr[0]==0) coeff_cr[0]=((x+y)&1 ? -1 : 1);
        //if (x==0 && y==0) coeff_cb[0]=coeff_cr[0]=-16;
        tb->coeff[0] = coeff_luma;
        tb->coeff[1] = coeff_cb;
        tb->coeff[2] = coeff_cr;

        cb->write_to_image(&img, x<<Log2CtbSize, y<<Log2CtbSize, Log2CtbSize, true);
        encode_ctb(&ectx, cb, x,y);

        int last = (y==sps.PicHeightInCtbsY-1 &&
                    x==sps.PicWidthInCtbsY-1);

        printf("wrote CTB at %d;%d\n",x*16,y*16);
        printf("write term bit: %d\n",last);
        writer.write_CABAC_term_bit(last);


        ectx.enc_cb_pool.free_all();
        ectx.enc_tb_pool.free_all();
        ectx.enc_pb_intra_pool.free_all();
      }
}


bool coeffzero(const int16_t* c,int n)
{
  for (int i=0;i<n;i++)
    if (c[i]) return false;

  return true;
}


void encode_image_FDCT_2()
{
  FILE* fh = fopen("paris_cif.yuv","rb");
  uint8_t* input[3];
  input[0] = (uint8_t*)malloc(352*288);
  input[1] = (uint8_t*)malloc(352*288/4);
  input[2] = (uint8_t*)malloc(352*288/4);
  fread(input[0],352,288,fh);
  fread(input[1],352,288/4,fh);
  fread(input[2],352,288/4,fh);
  fclose(fh);
  int stride = 352;

  int w = sps.pic_width_in_luma_samples;
  int h = sps.pic_height_in_luma_samples;

  img.alloc_image(w,h, de265_chroma_420, &sps, true, NULL /* no decctx */);
  img.alloc_encoder_data(&sps);

  initialize_CABAC_models(ectx.ctx_model, shdr.initType, shdr.SliceQPY);

  int Log2CtbSize = sps.Log2CtbSizeY;


  // encode CTB by CTB

  for (int y=0;y<sps.PicHeightInCtbsY;y++)
    for (int x=0;x<sps.PicWidthInCtbsY;x++)
      {
        int x0 = x<<Log2CtbSize;
        int y0 = y<<Log2CtbSize;

        enc_cb* cb = ectx.enc_cb_pool.get_new();
        cb->split_cu_flag = false;

        cb->cu_transquant_bypass_flag = false;
        cb->PredMode = MODE_INTRA;
        cb->PartMode = PART_2Nx2N;

        enc_pb_intra* pb = ectx.enc_pb_intra_pool.get_new();
        cb->intra_pb[0] = pb;
        pb->pred_mode = INTRA_DC;
        pb->pred_mode_chroma = INTRA_DC;


        enc_tb* tb = ectx.enc_tb_pool.get_new();
        cb->transform_tree = tb;

        tb->parent = NULL;
        tb->split_transform_flag = false;


        decode_intra_prediction(&img, x0,y0, pb->pred_mode, 16, 0);
        decode_intra_prediction(&img, x0/2,y0/2, pb->pred_mode_chroma, 8, 1);
        decode_intra_prediction(&img, x0/2,y0/2, pb->pred_mode_chroma, 8, 2);

        // subtract intra-prediction from input

        uint8_t* luma_plane = img.get_image_plane(0);
        uint8_t* cb_plane = img.get_image_plane(1);
        uint8_t* cr_plane = img.get_image_plane(2);
        int16_t blk[3][16*16];
        for (int y=0;y<16;y++)
          for (int x=0;x<16;x++)
            {
              blk[0][y*16+x] = input[0][(y0+y)*stride +x0+x] - luma_plane[(y0+y)*stride + x0+x];
            }

        for (int y=0;y<8;y++)
          for (int x=0;x<8;x++)
            {
              blk[1][y*8+x] = input[1][(y0/2+y)*stride/2 +(x0+x)/2] - cb_plane[(y0/2+y)*stride/2 + (x0/2+x)];
              blk[2][y*8+x] = input[2][(y0/2+y)*stride/2 +(x0+x)/2] - cr_plane[(y0/2+y)*stride/2 + (x0/2+x)];
            }


        int16_t coeff_luma[16*16], coeff_cb[8*8], coeff_cr[8*8];
        tb->coeff[0] = coeff_luma;
        tb->coeff[1] = coeff_cb;
        tb->coeff[2] = coeff_cr;

        fdct_16x16_8_fallback(coeff_luma, blk[0],16);
        fdct_8x8_8_fallback(coeff_cb, blk[1],8);
        fdct_8x8_8_fallback(coeff_cr, blk[2],8);

        logtrace(LogTransform,"raw DCT coefficients:\n");
        for (int y=0;y<16;y++) {
          logtrace(LogTransform,"  ");
          for (int x=0;x<16;x++) {
            logtrace(LogTransform,"*%3d ", coeff_luma[x+y*16]);
          }
          logtrace(LogTransform,"*\n");
        }


        for (int i=0;i<16*16;i++) {
          coeff_luma[i] = coeff_luma[i] *64 /16 * 64/57 ;
        }

        for (int i=0;i<8*8;i++) {
          coeff_cb[i] = coeff_cb[i] *32 /16 * 64/57 ;
          coeff_cr[i] = coeff_cr[i] *32 /16 * 64/57 ;
        }

        tb->cbf_luma = !coeffzero(coeff_luma,16*16);
        tb->cbf_cb   = false; // !coeffzero(coeff_cb,  8*8);
        tb->cbf_cr   = false; // !coeffzero(coeff_cr,  8*8);

        cb->write_to_image(&img, x<<Log2CtbSize, y<<Log2CtbSize, Log2CtbSize, true);
        encode_ctb(&ectx, cb, x,y);


        // decode into image

        transform_16x16_add_8_fallback(&luma_plane[(y0+y)*stride + x0+x],  coeff_luma, stride);

        for (int y=0;y<16;y++)
          for (int x=0;x<16;x++)
            {
              luma_plane[(y0+y)*stride + x0+x] = input[0][(y0+y)*stride +x0+x];
            }

        for (int y=0;y<8;y++)
          for (int x=0;x<8;x++)
            {
              cb_plane[(y0/2+y)*stride/2 + x0/2+x] = input[1][(y0/2+y)*stride/2 +x0/2+x];
              cr_plane[(y0/2+y)*stride/2 + x0/2+x] = input[2][(y0/2+y)*stride/2 +x0/2+x];
            }


        int last = (y==sps.PicHeightInCtbsY-1 &&
                    x==sps.PicWidthInCtbsY-1);

        printf("wrote CTB at %d;%d\n",x*16,y*16);
        printf("write term bit: %d\n",last);
        writer.write_CABAC_term_bit(last);


        ectx.enc_cb_pool.free_all();
        ectx.enc_tb_pool.free_all();
        ectx.enc_pb_intra_pool.free_all();
      }
}


template <class T> void printblk(T* data, int w)
{
  for (int y=0;y<w;y++,printf("\n"))
    for (int x=0;x<w;x++) {
      printf("%3d ",data[x+y*w]);
    }
}


void DCT_test()
{
  int16_t input[16*16];
  for (int i=0;i<16*16;i++)
    input[i] = 120; //(i%16)*10;

  printf("input\n");
  printblk(input,16);

  int16_t coeff[16*16];
  fdct_16x16_8_fallback(coeff, input,16);
  //fdct_8x8_8_fallback(coeff, input,16);
  //fdct_4x4_8_fallback(coeff, input,16);

  printf("coeff\n");
  printblk(coeff,16);

  for (int i=0;i<16*16;i++) {
    //coeff[i] *= 256;
  }

  uint8_t output[16*16];
  memset(output,0,16*16);
  transform_16x16_add_8_fallback(output, coeff, 16);

  printf("output\n");
  printblk(output,16);
}


extern void split_last_significant_position(int pos, int* prefix, int* suffix, int* nSuffixBits);

static void test_last_significant_coeff_pos()
{
  for (int i=0;i<64;i++)
    {
      int prefix,suffix,bits;
      split_last_significant_position(i,&prefix,&suffix,&bits);

      int v;
      if (prefix>3) {
        int b = (prefix>>1)-1;
        assert(b==bits);
        v = ((2+(prefix&1))<<b) + suffix;
      }
      else {
        v = prefix;
      }

      printf("%d : %d %d %d  -> %d\n",i,prefix,suffix,bits,v);
      assert(v==i);
    }
}


void write_stream_1()
{
  nal_header nal;

  // VPS

  vps.set_defaults(Profile_Main, 6,2);


  // SPS

  sps.set_defaults();
  sps.set_CB_log2size_range(4,4);
  sps.set_TB_log2size_range(4,4);
  sps.set_resolution(352,288);
  sps.compute_derived_values();

  // PPS

  pps.set_defaults();

  // turn off deblocking filter
  pps.deblocking_filter_control_present_flag = true;
  pps.deblocking_filter_override_enabled_flag = false;
  pps.pic_disable_deblocking_filter_flag = true;
  pps.pps_loop_filter_across_slices_enabled_flag = false;

  pps.set_derived_values(&sps);


  // slice

  shdr.set_defaults(&pps);
  shdr.slice_deblocking_filter_disabled_flag = true;
  shdr.slice_loop_filter_across_slices_enabled_flag = false;

  img.vps  = vps;
  img.sps  = sps;
  img.pps  = pps;

  ectx.img = &img;
  ectx.shdr = &shdr;
  ectx.cabac_encoder = &writer;

  //context_model ctx_model[CONTEXT_MODEL_TABLE_LENGTH];



  // write headers

  writer.write_startcode();
  nal.set(NAL_UNIT_VPS_NUT);
  nal.write(&writer);
  vps.write(&errqueue, &writer);
  writer.flush_VLC();

  writer.write_startcode();
  nal.set(NAL_UNIT_SPS_NUT);
  nal.write(&writer);
  sps.write(&errqueue, &writer);
  writer.flush_VLC();

  writer.write_startcode();
  nal.set(NAL_UNIT_PPS_NUT);
  nal.write(&writer);
  pps.write(&errqueue, &writer, &sps);
  writer.flush_VLC();

  writer.write_startcode();
  nal.set(NAL_UNIT_IDR_W_RADL);
  nal.write(&writer);
  shdr.write(&errqueue, &writer, &sps, &pps, nal.nal_unit_type);
  writer.skip_bits(1);
  writer.flush_VLC();

  //encode_image_coeffTest_1();
  //encode_image_FDCT_1();
  encode_image_FDCT_2();

  //encode_image(&ectx);
  writer.flush_CABAC();
}



int main(int argc, char** argv)
{
  de265_set_verbosity(3);

  init_scan_orders();
  alloc_and_init_significant_coeff_ctxIdx_lookupTable();

  de265_set_verbosity(3);

  DCT_test();

  write_stream_1();

  FILE* fh = fopen("out.bin","wb");
  fwrite(writer.data(), 1,writer.size(), fh);
  fclose(fh);

  return 0;
}
