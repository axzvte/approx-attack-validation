// Gate translation of third_party/evoapproxlib/add12se_5RP.c.
// Source SHA256: a6394aab970710abeec2467d65b21d74deff0e2582d7fc7150a6e0cfb9fd3eea
// EvoApproxLib (ehw.fit.vutbr.cz/approxlib), distributed under The MIT License.
// Cite: V. Mrazek, L. Sekanina, Z. Vasicek, "Libraries of Approximate Circuits:
// Automated Design and Application in CNN Accelerators", IEEE JETCAS 10(4), 2020.
// a is C symbol A, b is C symbol B. C signature/call order is (B,A).
// C's 64-bit intermediate signals are consumed only at bit zero. In particular
// dout_93 reduces the bit-zero of the 64-bit inversion to this one-bit NOT.
module add12se_5RP(input wire [11:0] a, b, output wire [12:0] y);
    wire dout_30, dout_31, dout_32, dout_33, dout_34, dout_35, dout_36;
    wire dout_37, dout_38, dout_39, dout_40, dout_41, dout_42, dout_43;
    wire dout_44, dout_45, dout_46, dout_47, dout_48, dout_52, dout_53;
    wire dout_54, dout_55, dout_56, dout_57, dout_58, dout_59, dout_60;
    wire dout_61, dout_62, dout_63, dout_66, dout_67, dout_68, dout_69;
    wire dout_70, dout_71, dout_72, dout_73, dout_74, dout_75, dout_76;
    wire dout_77, dout_78, dout_79, dout_82, dout_83, dout_84, dout_85;
    wire dout_86, dout_87, dout_88, dout_89, dout_90, dout_91, dout_93;
    wire dout_94, dout_95, dout_96, dout_97, dout_98, dout_99, dout_100;
    wire dout_101, dout_102, dout_103;

    assign dout_30 = a[3] & b[3];
    assign dout_31 = a[3] ^ b[3];
    assign dout_32 = a[4] & b[4];
    assign dout_33 = a[4] ^ b[4];
    assign dout_34 = a[5] & b[5];
    assign dout_35 = a[5] ^ b[5];
    assign dout_36 = a[6] & b[6];
    assign dout_37 = a[6] ^ b[6];
    assign dout_38 = a[7] & b[7];
    assign dout_39 = a[7] ^ b[7];
    assign dout_40 = a[8] & b[8];
    assign dout_41 = a[8] ^ b[8];
    assign dout_42 = a[9] & b[9];
    assign dout_43 = a[9] ^ b[9];
    assign dout_44 = a[10] & b[10];
    assign dout_45 = a[10] ^ b[10];
    assign dout_46 = a[11] & b[11];
    assign dout_47 = a[11] ^ b[11];
    assign dout_48 = a[11] ^ b[11];
    assign dout_52 = dout_33 & dout_30;
    assign dout_53 = dout_33 & dout_31;
    assign dout_54 = dout_32 | dout_52;
    assign dout_55 = dout_37 & dout_34;
    assign dout_56 = dout_37 & dout_35;
    assign dout_57 = dout_36 | dout_55;
    assign dout_58 = dout_41 & dout_38;
    assign dout_59 = dout_41 & dout_39;
    assign dout_60 = dout_40 | dout_58;
    assign dout_61 = dout_45 & dout_42;
    assign dout_62 = dout_45 & dout_43;
    assign dout_63 = dout_44 | dout_61;
    assign dout_66 = dout_56 & dout_54;
    assign dout_67 = dout_56 & dout_53;
    assign dout_68 = dout_57 | dout_66;
    assign dout_69 = dout_62 & dout_60;
    assign dout_70 = dout_62 & dout_59;
    assign dout_71 = dout_63 | dout_69;
    assign dout_72 = dout_67 & a[2];
    assign dout_73 = dout_68 | dout_72;
    assign dout_74 = dout_70 & dout_73;
    assign dout_75 = dout_71 | dout_74;
    assign dout_76 = dout_53 & a[2];
    assign dout_77 = dout_54 | dout_76;
    assign dout_78 = dout_59 & dout_73;
    assign dout_79 = dout_60 | dout_78;
    assign dout_82 = dout_31 & a[2];
    assign dout_83 = dout_30 | dout_82;
    assign dout_84 = dout_35 & dout_77;
    assign dout_85 = dout_34 | dout_84;
    assign dout_86 = dout_39 & dout_73;
    assign dout_87 = dout_38 | dout_86;
    assign dout_88 = dout_43 & dout_79;
    assign dout_89 = dout_42 | dout_88;
    assign dout_90 = dout_47 & dout_75;
    assign dout_91 = dout_46 | dout_90;
    assign dout_93 = ~a[2];
    assign dout_94 = dout_31 ^ a[2];
    assign dout_95 = dout_33 ^ dout_83;
    assign dout_96 = dout_35 ^ dout_77;
    assign dout_97 = dout_37 ^ dout_85;
    assign dout_98 = dout_39 ^ dout_73;
    assign dout_99 = dout_41 ^ dout_87;
    assign dout_100 = dout_43 ^ dout_79;
    assign dout_101 = dout_45 ^ dout_89;
    assign dout_102 = dout_47 ^ dout_75;
    assign dout_103 = dout_48 ^ dout_91;

    assign y = {dout_103, dout_102, dout_101, dout_100, dout_99, dout_98,
                dout_97, dout_96, dout_95, dout_94, dout_93, b[2], a[0]};
endmodule
