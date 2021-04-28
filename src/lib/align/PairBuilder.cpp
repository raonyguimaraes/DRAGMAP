/**
 ** DRAGEN Open Source Software
 ** Copyright (c) 2019-2020 Illumina, Inc.
 ** All rights reserved.
 **
 ** This software is provided under the terms and conditions of the
 ** GNU GENERAL PUBLIC LICENSE Version 3
 **
 ** You should have received a copy of the GNU GENERAL PUBLIC LICENSE Version 3
 ** along with this program. If not, see
 ** <https://github.com/illumina/licenses/>.
 **
 **/

#include "align/PairBuilder.hpp"
#include "align/AlignmentRescue.hpp"
#include "align/SinglePicker.hpp"
#include "align/Tlen.hpp"

namespace dragenos {
namespace align {

//-- This ROM contains the phred-scale (-10log10) probability from a normal PDF, with the output scaled to 1
// at input 0,
//-- and the input scaled to saturate 8 bits (0xFF) at the end of a 9-bit range (511).
//--    N = 0..511:  rom(N) = round(-10*log10(standardNormalPDF(N/47.125)/standardNormalPDF(0)))
//-- To use it, given an expected insert-size standard deviation "sigma" we have config register
//--    sigma_factor = min(0xFFFF, round(0x2F200/sigma)), interpreted as 4.12 bit fixed point.
//--    (This saturates when sigma is sligtly less than 3.)
//-- Then look up:
//--    N = int(sigma_factor * abs(insert_size - mean))
//--  constant petab_rom_c : petab_rom_t := (
//--    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00",
//--    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01",
//--    0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x02, 0x02, 0x02, 0x02, 0x02, 0x02, 0x02, 0x02",
//--    0x02, 0x02, 0x02, 0x03, 0x03, 0x03, 0x03, 0x03, 0x03, 0x03, 0x03, 0x03, 0x04, 0x04, 0x04, 0x04",
//--    0x04, 0x04, 0x04, 0x04, 0x05, 0x05, 0x05, 0x05, 0x05, 0x05, 0x05, 0x06, 0x06, 0x06, 0x06, 0x06",
//--    0x06, 0x06, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x09, 0x09",
//--    0x09, 0x09, 0x09, 0x0A, 0x0A, 0x0A, 0x0A, 0x0A, 0x0B, 0x0B, 0x0B, 0x0B, 0x0B, 0x0C, 0x0C, 0x0C",
//--    0x0C, 0x0C, 0x0D, 0x0D, 0x0D, 0x0D, 0x0E, 0x0E, 0x0E, 0x0E, 0x0F, 0x0F, 0x0F, 0x0F, 0x10, 0x10",
//--    0x10, 0x10, 0x11, 0x11, 0x11, 0x11, 0x12, 0x12, 0x12, 0x12, 0x13, 0x13, 0x13, 0x13, 0x14, 0x14",
//--    0x14, 0x15, 0x15, 0x15, 0x15, 0x16, 0x16, 0x16, 0x17, 0x17, 0x17, 0x17, 0x18, 0x18, 0x18, 0x19",
//--    0x19, 0x19, 0x1A, 0x1A, 0x1A, 0x1B, 0x1B, 0x1B, 0x1C, 0x1C, 0x1C, 0x1D, 0x1D, 0x1D, 0x1E, 0x1E",
//--    0x1E, 0x1F, 0x1F, 0x1F, 0x20, 0x20, 0x20, 0x21, 0x21, 0x21, 0x22, 0x22, 0x23, 0x23, 0x23, 0x24",
//--    0x24, 0x24, 0x25, 0x25, 0x26, 0x26, 0x26, 0x27, 0x27, 0x28, 0x28, 0x28, 0x29, 0x29, 0x29, 0x2A",
//--    0x2A, 0x2B, 0x2B, 0x2C, 0x2C, 0x2C, 0x2D, 0x2D, 0x2E, 0x2E, 0x2E, 0x2F, 0x2F, 0x30, 0x30, 0x31",
//--    0x31, 0x32, 0x32, 0x32, 0x33, 0x33, 0x34, 0x34, 0x35, 0x35, 0x36, 0x36, 0x36, 0x37, 0x37, 0x38",
//--    0x38, 0x39, 0x39, 0x3A, 0x3A, 0x3B, 0x3B, 0x3C, 0x3C, 0x3D, 0x3D, 0x3E, 0x3E, 0x3F, 0x3F, 0x40",
//--    0x40, 0x41, 0x41, 0x42, 0x42, 0x43, 0x43, 0x44, 0x44, 0x45, 0x45, 0x46, 0x46, 0x47, 0x47, 0x48",
//--    0x48, 0x49, 0x49, 0x4A, 0x4A, 0x4B, 0x4C, 0x4C, 0x4D, 0x4D, 0x4E, 0x4E, 0x4F, 0x4F, 0x50, 0x51",
//--    0x51, 0x52, 0x52, 0x53, 0x53, 0x54, 0x55, 0x55, 0x56, 0x56, 0x57, 0x57, 0x58, 0x59, 0x59, 0x5A",
//--    0x5A, 0x5B, 0x5C, 0x5C, 0x5D, 0x5D, 0x5E, 0x5F, 0x5F, 0x60, 0x60, 0x61, 0x62, 0x62, 0x63, 0x64",
//--    0x64, 0x65, 0x65, 0x66, 0x67, 0x67, 0x68, 0x69, 0x69, 0x6A, 0x6A, 0x6B, 0x6C, 0x6C, 0x6D, 0x6E",
//--    0x6E, 0x6F, 0x70, 0x70, 0x71, 0x72, 0x72, 0x73, 0x74, 0x74, 0x75, 0x76, 0x76, 0x77, 0x78, 0x78",
//--    0x79, 0x7A, 0x7B, 0x7B, 0x7C, 0x7D, 0x7D, 0x7E, 0x7F, 0x7F, 0x80, 0x81, 0x82, 0x82, 0x83, 0x84",
//--    0x84, 0x85, 0x86, 0x87, 0x87, 0x88, 0x89, 0x8A, 0x8A, 0x8B, 0x8C, 0x8C, 0x8D, 0x8E, 0x8F, 0x8F",
//--    0x90, 0x91, 0x92, 0x92, 0x93, 0x94, 0x95, 0x95, 0x96, 0x97, 0x98, 0x99, 0x99, 0x9A, 0x9B, 0x9C",
//--    0x9C, 0x9D, 0x9E, 0x9F, 0xA0, 0xA0, 0xA1, 0xA2, 0xA3, 0xA4, 0xA4, 0xA5, 0xA6, 0xA7, 0xA8, 0xA8",
//--    0xA9, 0xAA, 0xAB, 0xAC, 0xAC, 0xAD, 0xAE, 0xAF, 0xB0, 0xB1, 0xB1, 0xB2, 0xB3, 0xB4, 0xB5, 0xB6",
//--    0xB6, 0xB7, 0xB8, 0xB9, 0xBA, 0xBB, 0xBC, 0xBC, 0xBD, 0xBE, 0xBF, 0xC0, 0xC1, 0xC2, 0xC3, 0xC3",
//--    0xC4, 0xC5, 0xC6, 0xC7, 0xC8, 0xC9, 0xCA, 0xCA, 0xCB, 0xCC, 0xCD, 0xCE, 0xCF, 0xD0, 0xD1, 0xD2",
//--    0xD3, 0xD3, 0xD4, 0xD5, 0xD6, 0xD7, 0xD8, 0xD9, 0xDA, 0xDB, 0xDC, 0xDD, 0xDE, 0xDE, 0xDF, 0xE0",
//--    0xE1, 0xE2, 0xE3, 0xE4, 0xE5, 0xE6, 0xE7, 0xE8, 0xE9, 0xEA, 0xEB, 0xEC, 0xED, 0xEE, 0xEF, 0xF0",
//--    0xF1, 0xF2, 0xF2, 0xF3, 0xF4, 0xF5, 0xF6, 0xF7, 0xF8, 0xF9, 0xFA, 0xFB, 0xFC, 0xFD, 0xFE, 0xFF");
//-- Alternate version matching legacy DRAGEN penalties defined by Gaussian 2-tail area ("P-value"):
//--    N = 0..511:  rom(N) = round(-10*log10(2*standardNormalCDF(-N/47.125)))
//-- These penalties are higher than the above, e.g.:  0 -> 0, 2 -> 5, 6 -> 10, 15 -> 20, 33 -> 40, 71 -> 80.
//-- They saturate at 0xFF a little sooner.  Although the PDF version above is theoretically more correct
// assuming
//-- the normal distribution is true, we saw some accuracy degradation, so we are rolling back to the legacy
// definition.
static const int petab_rom_c[] = {
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01,
    0x01, 0x02, 0x02, 0x02, 0x02, 0x02, 0x02, 0x02, 0x02, 0x02, 0x02, 0x03, 0x03, 0x03, 0x03, 0x03, 0x03,
    0x03, 0x03, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04, 0x05, 0x05, 0x05, 0x05, 0x05, 0x05, 0x05,
    0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x08, 0x08, 0x08, 0x08,
    0x08, 0x08, 0x09, 0x09, 0x09, 0x09, 0x09, 0x0A, 0x0A, 0x0A, 0x0A, 0x0A, 0x0A, 0x0B, 0x0B, 0x0B, 0x0B,
    0x0B, 0x0C, 0x0C, 0x0C, 0x0C, 0x0D, 0x0D, 0x0D, 0x0D, 0x0D, 0x0E, 0x0E, 0x0E, 0x0E, 0x0E, 0x0F, 0x0F,
    0x0F, 0x0F, 0x10, 0x10, 0x10, 0x10, 0x11, 0x11, 0x11, 0x11, 0x12, 0x12, 0x12, 0x12, 0x13, 0x13, 0x13,
    0x13, 0x14, 0x14, 0x14, 0x14, 0x15, 0x15, 0x15, 0x16, 0x16, 0x16, 0x16, 0x17, 0x17, 0x17, 0x18, 0x18,
    0x18, 0x18, 0x19, 0x19, 0x19, 0x1A, 0x1A, 0x1A, 0x1A, 0x1B, 0x1B, 0x1B, 0x1C, 0x1C, 0x1C, 0x1D, 0x1D,
    0x1D, 0x1E, 0x1E, 0x1E, 0x1F, 0x1F, 0x1F, 0x20, 0x20, 0x20, 0x21, 0x21, 0x21, 0x22, 0x22, 0x22, 0x23,
    0x23, 0x23, 0x24, 0x24, 0x25, 0x25, 0x25, 0x26, 0x26, 0x26, 0x27, 0x27, 0x27, 0x28, 0x28, 0x29, 0x29,
    0x29, 0x2A, 0x2A, 0x2B, 0x2B, 0x2B, 0x2C, 0x2C, 0x2D, 0x2D, 0x2D, 0x2E, 0x2E, 0x2F, 0x2F, 0x2F, 0x30,
    0x30, 0x31, 0x31, 0x32, 0x32, 0x32, 0x33, 0x33, 0x34, 0x34, 0x35, 0x35, 0x35, 0x36, 0x36, 0x37, 0x37,
    0x38, 0x38, 0x39, 0x39, 0x39, 0x3A, 0x3A, 0x3B, 0x3B, 0x3C, 0x3C, 0x3D, 0x3D, 0x3E, 0x3E, 0x3F, 0x3F,
    0x40, 0x40, 0x41, 0x41, 0x42, 0x42, 0x42, 0x43, 0x43, 0x44, 0x44, 0x45, 0x45, 0x46, 0x46, 0x47, 0x48,
    0x48, 0x49, 0x49, 0x4A, 0x4A, 0x4B, 0x4B, 0x4C, 0x4C, 0x4D, 0x4D, 0x4E, 0x4E, 0x4F, 0x4F, 0x50, 0x51,
    0x51, 0x52, 0x52, 0x53, 0x53, 0x54, 0x54, 0x55, 0x55, 0x56, 0x57, 0x57, 0x58, 0x58, 0x59, 0x59, 0x5A,
    0x5B, 0x5B, 0x5C, 0x5C, 0x5D, 0x5E, 0x5E, 0x5F, 0x5F, 0x60, 0x61, 0x61, 0x62, 0x62, 0x63, 0x64, 0x64,
    0x65, 0x65, 0x66, 0x67, 0x67, 0x68, 0x68, 0x69, 0x6A, 0x6A, 0x6B, 0x6C, 0x6C, 0x6D, 0x6E, 0x6E, 0x6F,
    0x6F, 0x70, 0x71, 0x71, 0x72, 0x73, 0x73, 0x74, 0x75, 0x75, 0x76, 0x77, 0x77, 0x78, 0x79, 0x79, 0x7A,
    0x7B, 0x7B, 0x7C, 0x7D, 0x7D, 0x7E, 0x7F, 0x7F, 0x80, 0x81, 0x82, 0x82, 0x83, 0x84, 0x84, 0x85, 0x86,
    0x86, 0x87, 0x88, 0x89, 0x89, 0x8A, 0x8B, 0x8B, 0x8C, 0x8D, 0x8E, 0x8E, 0x8F, 0x90, 0x91, 0x91, 0x92,
    0x93, 0x94, 0x94, 0x95, 0x96, 0x97, 0x97, 0x98, 0x99, 0x9A, 0x9A, 0x9B, 0x9C, 0x9D, 0x9D, 0x9E, 0x9F,
    0xA0, 0xA0, 0xA1, 0xA2, 0xA3, 0xA4, 0xA4, 0xA5, 0xA6, 0xA7, 0xA8, 0xA8, 0xA9, 0xAA, 0xAB, 0xAC, 0xAC,
    0xAD, 0xAE, 0xAF, 0xB0, 0xB0, 0xB1, 0xB2, 0xB3, 0xB4, 0xB5, 0xB5, 0xB6, 0xB7, 0xB8, 0xB9, 0xBA, 0xBA,
    0xBB, 0xBC, 0xBD, 0xBE, 0xBF, 0xBF, 0xC0, 0xC1, 0xC2, 0xC3, 0xC4, 0xC5, 0xC5, 0xC6, 0xC7, 0xC8, 0xC9,
    0xCA, 0xCB, 0xCC, 0xCC, 0xCD, 0xCE, 0xCF, 0xD0, 0xD1, 0xD2, 0xD3, 0xD4, 0xD4, 0xD5, 0xD6, 0xD7, 0xD8,
    0xD9, 0xDA, 0xDB, 0xDC, 0xDD, 0xDD, 0xDE, 0xDF, 0xE0, 0xE1, 0xE2, 0xE3, 0xE4, 0xE5, 0xE6, 0xE7, 0xE8,
    0xE9, 0xEA, 0xEA, 0xEB, 0xEC, 0xED, 0xEE, 0xEF, 0xF0, 0xF1, 0xF2, 0xF3, 0xF4, 0xF5, 0xF6, 0xF7, 0xF8,
    0xF9, 0xFA, 0xFB, 0xFC, 0xFD, 0xFE, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
    0xFF, 0xFF};

int PairBuilder::computePairPenalty(
    const InsertSizeParameters& insertSizeParameters,
    const ReadPair&             readPair,
    const map::SeedChain*       c1,
    const map::SeedChain*       c2,
    const bool                  properPair) const
{
  int m2a_penalty = -1;
  if (!c1 || !c2 || !properPair) {
    m2a_penalty = aln_cfg_unpaired_pen_;
  } else {
    //  std::cerr << "c1:" << c1 << " c2:" << c2 <<std::endl;
    const int inp_result_qry_end_gap =
        readPair[0].getLength() - c1->lastReadBase() - 1;  //back().getSeed().getReadPosition() - 1;
    //  std::cerr << "inp_result_qry_end_gap:" << inp_result_qry_end_gap << std::endl;
    const int result_rrec_qry_end_gap =
        readPair[1].getLength() - c2->lastReadBase() - 1;  //back().getSeed().getReadPosition() - 1;
    //  std::cerr << "result_rrec_qry_end_gap:" << result_rrec_qry_end_gap << std::endl;

    ////  int gap_v = !c1.isReverseComplement() ? c1.front().getSeed().getReadPosition() : inp_result_qry_end_gap;
    //  const int inp_eff_beg = hashtableConfig_.convertToReferenceCoordinates(c1.firstReferencePosition()).second;// - gap_v;
    ////  gap_v = c1.isReverseComplement() ? c1.front().getSeed().getReadPosition() : inp_result_qry_end_gap;
    //  const int inp_eff_end = hashtableConfig_.convertToReferenceCoordinates(c1.lastReferencePosition()).second;// + gap_v;
    ////  gap_v = !c2.isReverseComplement() ? c2.front().getSeed().getReadPosition() : result_rrec_qry_end_gap;
    //  const int rct_eff_beg = hashtableConfig_.convertToReferenceCoordinates(c2.firstReferencePosition()).second;// - gap_v;
    ////  gap_v = c2.isReverseComplement() ? c2.front().getSeed().getReadPosition() : result_rrec_qry_end_gap;
    //  const int rct_eff_end = hashtableConfig_.convertToReferenceCoordinates(c2.lastReferencePosition()).second;// + gap_v;

    const int inp_eff_beg = c1->firstReferencePosition();
    const int inp_eff_end = c1->lastReferencePosition();
    const int rct_eff_beg = c2->firstReferencePosition();
    const int rct_eff_end = c2->lastReferencePosition();

    using Orientation = InsertSizeParameters::Orientation;

    uint32_t insert_beg = 0;
    uint32_t insert_end = 0;
    // -- Forward-forward orientation (same as reverse-reverse): pick the outermost begin and end
    if (insertSizeParameters.orientation_ == Orientation::pe_orient_ff_c or
        insertSizeParameters.orientation_ == Orientation::pe_orient_rr_c) {
      //    std::cerr << "ff or rr" << std::endl;
      insert_beg = std::min(inp_eff_beg, rct_eff_beg);
      insert_end = std::max(inp_eff_end, rct_eff_end);
      //    wrong_beg_v := '0' & inp_result.ref_beg;
      //    wrong_end_v := '0' & inp_result.ref_end;
    }
    //-- Forward-reverse: beginning from the forward one, end from the reverse one
    //-- Reverse-forward: beginning from the reverse one, end from the forward one
    else if ((insertSizeParameters.orientation_ == Orientation::pe_orient_fr_c) ^ c1->isReverseComplement()) {
      //    std::cerr << "fr" << std::endl;
      insert_beg = inp_eff_beg;
      insert_end = rct_eff_end;
      //    wrong_beg_v := '0' & result_rrec.ref_beg;
      //    wrong_end_v := '0' & inp_result.ref_end;
    } else {
      //    std::cerr << "rf?" << std::endl;
      insert_beg = rct_eff_beg;
      insert_end = inp_eff_end;
      //    wrong_beg_v := '0' & inp_result.ref_beg;
      //    wrong_end_v := '0' & result_rrec.ref_end;
    }

    const int insert_len = insert_end - insert_beg + 1;
    //    std::cerr << "insert_beg:" << insert_beg << " insert_end:" << insert_end << " insert_len:" << insert_len << std::endl;

    const int insert_diff = std::abs(insert_len - insertSizeParameters.mean_);
    //    std::cerr << "insert_diff:" << insert_diff << std::endl;
    const unsigned ins_prod = insert_diff * insertSizeParameters.getSigmaFactor();
    //  std::cerr << "sigma factor:" << insertSizeParameters.getSigmaFactor() << std::endl;
    static const int sigma_factor_frac_bits_c = 12;
    static const int petab_addr_bits_c        = 9;
    const unsigned   ins_prod_q = ((1 << petab_addr_bits_c) - 1) & (ins_prod >> sigma_factor_frac_bits_c);
    //    std::cerr << "ins_prod:" << ins_prod << " ins_prod_q:" << ins_prod_q << std::endl;

    m2a_penalty = ins_prod_q < sizeof(petab_rom_c) / sizeof(*petab_rom_c) ? petab_rom_c[ins_prod_q]
                                                                          : aln_cfg_unpaired_pen_;
  }

  const double m2a_scale = mapq2aln(similarity_.getSnpCost(), readPair.getLength());
  //  std::cerr << "m2a_scale:" << m2a_scale << std::endl;
  const int m2a_prod             = m2a_scale * m2a_penalty;
  const int m2a_prod_frac_bits_c = 10;
  const int m2a_prod_pen         = m2a_prod;  // >> m2a_prod_frac_bits_c;

  //    std::cerr << "m2a_prod_pen:" << m2a_prod_pen << std::endl;
  return m2a_prod_pen;
}

/**
 * \brief Find second best pair such that the read at readIdx is not part of it
 * \param nearSub - On return, count of alignments that are withing 1 SNP of suboptimal.
 *        Unchanged if no suboptimal found
 */
AlignmentPairs::const_iterator PairBuilder::findSecondBest(
    const int                            averageReadLength,
    const AlignmentPairs&                pairs,
    const UnpairedAlignments&            unpairedAlignments,
    const AlignmentPairs::const_iterator best,
    int                                  readIdx,
    int&                                 sub_count) const
{
  AlignmentPairs::const_iterator ret = pairs.end();
  for (AlignmentPairs::const_iterator it = pairs.begin(); pairs.end() != it; ++it) {
    if ((pairs.end() == ret || ret->getScore() < it->getScore()) &&
        !best->at(readIdx).isDuplicate(it->at(readIdx)) && best->at(readIdx).isOverlap(it->at(readIdx))) {
      ret = it;
    }
  }

  if (pairs.end() != ret) {
    const ScoreType list_pe_max = ret->getScore();
    const ScoreType list_pe_min = list_pe_max - similarity_.getSnpCost();
    //  std::cerr << "list_pe_max:" << list_pe_max << std::endl;
    //  std::cerr << "list_pe_min:" << list_pe_min << std::endl;
    for (AlignmentPairs::const_iterator it = pairs.begin(); pairs.end() != it; ++it) {
      if (it->getScore() > list_pe_min && it->getScore() <= list_pe_max) {
        //        std::cerr << "near sub:" << *it << std::endl;
        ++sub_count;
      }
    }

    ScoreType other_best_scr = alnMinScore_;
    for (const Alignment& oe : unpairedAlignments[!readIdx]) {
      if (&oe != &best->at(!readIdx)) {
        other_best_scr = std::max(other_best_scr, oe.getScore());
      }
    }

    const double    m2a_scale      = mapq2aln(similarity_.getSnpCost(), averageReadLength);
    const ScoreType scaled_max_pen = m2a_scale * aln_cfg_unpaired_pen_;  //27;

    // std::cerr << "second best: " << *ret << std::endl;
    // std::cerr << "other_best_scr: " << other_best_scr << std::endl;
    // std::cerr << "scaled_max_pen: " << scaled_max_pen << std::endl;
    // const ScoreType list_se_max = ret->getScore() - best_other_end->getScore() + scaled_max_pen;
    const ScoreType list_se_max = ret->getScore() - other_best_scr + scaled_max_pen;
    const ScoreType list_se_min = list_se_max - similarity_.getSnpCost();
    //  std::cerr << "list_se_max:" << list_se_max << std::endl;
    //  std::cerr << "list_se_min:" << list_se_min << std::endl;

    for (Alignments::const_iterator it = unpairedAlignments[readIdx].begin();
         unpairedAlignments[readIdx].end() != it;
         ++it) {
      if (!it->isUnmapped() && it->getScore() > list_se_min && it->getScore() <= list_se_max) {
        //      std::cerr << "sub_count:" << *it << " scaled_max_pen:" << scaled_max_pen << std::endl;
        ++sub_count;
      }
    }
  }

  return ret;
}

ScoreType PairBuilder::findSecondBestScore(
    const int                            averageReadLength,
    const AlignmentPairs&                pairs,
    const UnpairedAlignments&            unpairedAlignments,
    const AlignmentPairs::const_iterator best,
    int                                  readIdx,
    int&                                 sub_count,
    ScoreType&                           secondBestPeScore) const
{
  AlignmentPairs::const_iterator secondBest =
      findSecondBest(averageReadLength, pairs, unpairedAlignments, best, readIdx, sub_count);

  int             se_sub_count      = 0;
  const ScoreType secondBestSeScore = align::findSecondBestScore(
      similarity_.getSnpCost(), unpairedAlignments[readIdx], &best->at(readIdx), se_sub_count);

  if (pairs.end() != secondBest) {
    secondBestPeScore = secondBest->getScore();
    return std::max(secondBestSeScore, secondBest->at(readIdx).getScore());
  }

  return secondBestSeScore;
}

void PairBuilder::updateEndMapq(
    const int                 averageReadLength,
    AlignmentPairs&           pairs,
    const UnpairedAlignments& unpairedAlignments,
    AlignmentPairs::iterator  best,
    const int                 readIdx) const
{
  if (best->at(readIdx).isUnmapped()) {
    best->at(readIdx).setMapq(0);
  } else {
    int             sub_count           = 0;
    ScoreType       secondBestPairScore = INVALID_SCORE;
    const ScoreType secondBestScore     = findSecondBestScore(
        averageReadLength, pairs, unpairedAlignments, best, readIdx, sub_count, secondBestPairScore);
    //        std::cerr << "r" << readIdx << " sub_count:" << sub_count << std::endl;
    const MapqType a2m_mapq = computeMapq(
        similarity_.getSnpCost(),
        best->getScore(),
        std::max(alnMinScore_, secondBestPairScore),
        std::max(aln_cfg_mapq_min_len_, averageReadLength));
    //        std::cerr << "r" << readIdx << " a2m_mapq:" << a2m_mapq << std::endl;
    const MapqType sub_mapq_pen_v = sub_count ? 3.0 * std::log2(sub_count) : 0;
    //        std::cerr << "r" << readIdx << " sub_mapq_pen_v:" << sub_mapq_pen_v << std::endl;
    best->at(readIdx).setMapq(a2m_mapq - sub_mapq_pen_v);
    best->at(readIdx).setXs(secondBestScore >= alnMinScore_ ? secondBestScore : INVALID_SCORE);
  }
}

void PairBuilder::updateMapq(
    const int                 readLength,
    AlignmentPairs&           pairs,
    const UnpairedAlignments& unpairedAlignments,
    AlignmentPairs::iterator  best) const
{
  updateEndMapq(readLength, pairs, unpairedAlignments, best, 0);
  updateEndMapq(readLength, pairs, unpairedAlignments, best, 1);
}

AlignmentPairs::iterator PairBuilder::pickBest(
    const ReadPair&           readPair,
    AlignmentPairs&           alignmentPairs,
    const UnpairedAlignments& unpairedAlignments) const
{
  if (alignmentPairs.empty()) {
    return alignmentPairs.end();
  }

  AlignmentPairs::iterator best = std::max_element(
      alignmentPairs.begin(),
      alignmentPairs.end(),
      [](const AlignmentPair& left, const AlignmentPair& right) {
        return left.getScore() < right.getScore();
      });

  if (true == best->at(0).getIneligibilityStatus() and true == best->at(1).getIneligibilityStatus())
    return alignmentPairs.end();

  updateMapq(readPair.getLength(), alignmentPairs, unpairedAlignments, best);
  //  std::cerr << "best: " << *best << std::endl;

  return best;
}

}  // namespace align
}  // namespace dragenos
