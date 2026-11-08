// Generated from bundled EvoApproxLib C; do not edit.

/***
* This code is a part of EvoApproxLib library (ehw.fit.vutbr.cz/approxlib) distributed under The MIT License.
* When used, please cite the following article(s): V. Mrazek, L. Sekanina, Z. Vasicek "Libraries of Approximate Circuits: Automated Design and Application in CNN Accelerators" IEEE Journal on Emerging and Selected Topics in Circuits and Systems, Vol 10, No 4, 2020 
* This file contains a circuit from a sub-set of pareto optimal circuits with respect to the pwr and mae parameters
***/
// MAE% = 0.022 %
// MAE = 0.9 
// WCE% = 0.049 %
// WCE = 2.0 
// WCRE% = 100.00 %
// EP% = 68.75 %
// MRE% = 0.33 %
// MSE = 1.2 
// PDK45_PWR = 0.047 mW
// PDK45_AREA = 97.1 um2
// PDK45_DELAY = 0.84 ns

module approx_5L8(input wire [11:0] A, B, output wire [12:0] Y);
wire [63:0] dout_25;
wire [63:0] dout_28;
wire [63:0] dout_29;
wire [63:0] dout_30;
wire [63:0] dout_31;
wire [63:0] dout_32;
wire [63:0] dout_33;
wire [63:0] dout_34;
wire [63:0] dout_35;
wire [63:0] dout_36;
wire [63:0] dout_37;
wire [63:0] dout_38;
wire [63:0] dout_39;
wire [63:0] dout_40;
wire [63:0] dout_41;
wire [63:0] dout_42;
wire [63:0] dout_43;
wire [63:0] dout_44;
wire [63:0] dout_45;
wire [63:0] dout_46;
wire [63:0] dout_47;
wire [63:0] dout_48;
wire [63:0] dout_50;
wire [63:0] dout_52;
wire [63:0] dout_53;
wire [63:0] dout_54;
wire [63:0] dout_55;
wire [63:0] dout_56;
wire [63:0] dout_57;
wire [63:0] dout_58;
wire [63:0] dout_59;
wire [63:0] dout_60;
wire [63:0] dout_61;
wire [63:0] dout_62;
wire [63:0] dout_63;
wire [63:0] dout_65;
wire [63:0] dout_66;
wire [63:0] dout_67;
wire [63:0] dout_68;
wire [63:0] dout_69;
wire [63:0] dout_70;
wire [63:0] dout_71;
wire [63:0] dout_72;
wire [63:0] dout_73;
wire [63:0] dout_74;
wire [63:0] dout_75;
wire [63:0] dout_76;
wire [63:0] dout_77;
wire [63:0] dout_78;
wire [63:0] dout_79;
wire [63:0] dout_82;
wire [63:0] dout_83;
wire [63:0] dout_84;
wire [63:0] dout_85;
wire [63:0] dout_86;
wire [63:0] dout_87;
wire [63:0] dout_88;
wire [63:0] dout_89;
wire [63:0] dout_90;
wire [63:0] dout_91;
wire [63:0] dout_92;
wire [63:0] dout_93;
wire [63:0] dout_94;
wire [63:0] dout_95;
wire [63:0] dout_96;
wire [63:0] dout_97;
wire [63:0] dout_98;
wire [63:0] dout_99;
wire [63:0] dout_100;
wire [63:0] dout_101;
wire [63:0] dout_102;
wire [63:0] dout_103;
assign dout_25 = ((B >> 64'd1)&64'd1)&((B >> 64'd7)&64'd1);
assign dout_28 = ((A >> 64'd2)&64'd1)&((B >> 64'd2)&64'd1);
assign dout_29 = ((A >> 64'd2)&64'd1)^((B >> 64'd2)&64'd1);
assign dout_30 = ((A >> 64'd3)&64'd1)&((B >> 64'd3)&64'd1);
assign dout_31 = ((A >> 64'd3)&64'd1)^((B >> 64'd3)&64'd1);
assign dout_32 = ((A >> 64'd4)&64'd1)&((B >> 64'd4)&64'd1);
assign dout_33 = ((A >> 64'd4)&64'd1)^((B >> 64'd4)&64'd1);
assign dout_34 = ((A >> 64'd5)&64'd1)&((B >> 64'd5)&64'd1);
assign dout_35 = ((A >> 64'd5)&64'd1)^((B >> 64'd5)&64'd1);
assign dout_36 = ((A >> 64'd6)&64'd1)&((B >> 64'd6)&64'd1);
assign dout_37 = ((A >> 64'd6)&64'd1)^((B >> 64'd6)&64'd1);
assign dout_38 = ((A >> 64'd7)&64'd1)&((B >> 64'd7)&64'd1);
assign dout_39 = ((A >> 64'd7)&64'd1)^((B >> 64'd7)&64'd1);
assign dout_40 = ((A >> 64'd8)&64'd1)&((B >> 64'd8)&64'd1);
assign dout_41 = ((A >> 64'd8)&64'd1)^((B >> 64'd8)&64'd1);
assign dout_42 = ((A >> 64'd9)&64'd1)&((B >> 64'd9)&64'd1);
assign dout_43 = ((A >> 64'd9)&64'd1)^((B >> 64'd9)&64'd1);
assign dout_44 = ((A >> 64'd10)&64'd1)&((B >> 64'd10)&64'd1);
assign dout_45 = ((A >> 64'd10)&64'd1)^((B >> 64'd10)&64'd1);
assign dout_46 = ((A >> 64'd11)&64'd1)&((B >> 64'd11)&64'd1);
assign dout_47 = ((A >> 64'd11)&64'd1)^((B >> 64'd11)&64'd1);
assign dout_48 = ((A >> 64'd11)&64'd1)^((B >> 64'd11)&64'd1);
assign dout_50 = dout_29&((A >> 64'd1)&64'd1);
assign dout_52 = dout_33&dout_30;
assign dout_53 = dout_33&dout_31;
assign dout_54 = dout_32|dout_52;
assign dout_55 = dout_37&dout_34;
assign dout_56 = dout_37&dout_35;
assign dout_57 = dout_36|dout_55;
assign dout_58 = dout_41&dout_38;
assign dout_59 = dout_41&dout_39;
assign dout_60 = dout_40|dout_58;
assign dout_61 = dout_45&dout_42;
assign dout_62 = dout_45&dout_43;
assign dout_63 = dout_44|dout_61;
assign dout_65 = dout_28|dout_50;
assign dout_66 = dout_56&dout_54;
assign dout_67 = dout_56&dout_53;
assign dout_68 = dout_57|dout_66;
assign dout_69 = dout_62&dout_60;
assign dout_70 = dout_62&dout_59;
assign dout_71 = dout_63|dout_69;
assign dout_72 = dout_67&dout_65;
assign dout_73 = dout_68|dout_72;
assign dout_74 = dout_70&dout_73;
assign dout_75 = dout_71|dout_74;
assign dout_76 = dout_53&dout_65;
assign dout_77 = dout_54|dout_76;
assign dout_78 = dout_59&dout_73;
assign dout_79 = dout_60|dout_78;
assign dout_82 = dout_31&dout_65;
assign dout_83 = dout_30|dout_82;
assign dout_84 = dout_35&dout_77;
assign dout_85 = dout_34|dout_84;
assign dout_86 = dout_39&dout_73;
assign dout_87 = dout_38|dout_86;
assign dout_88 = dout_43&dout_79;
assign dout_89 = dout_42|dout_88;
assign dout_90 = dout_47&dout_75;
assign dout_91 = dout_46|dout_90;
assign dout_92 = ((A >> 64'd1)&64'd1)^64'd18446744073709551615;
assign dout_93 = dout_29^((A >> 64'd1)&64'd1);
assign dout_94 = dout_31^dout_65;
assign dout_95 = dout_33^dout_83;
assign dout_96 = dout_35^dout_77;
assign dout_97 = dout_37^dout_85;
assign dout_98 = dout_39^dout_73;
assign dout_99 = dout_41^dout_87;
assign dout_100 = dout_43^dout_79;
assign dout_101 = dout_45^dout_89;
assign dout_102 = dout_47^dout_75;
assign dout_103 = dout_48^dout_91;
assign Y = ((dout_25&64'd1) << 64'd0) | ((dout_92&64'd1) << 64'd1) | ((dout_93&64'd1) << 64'd2) | ((dout_94&64'd1) << 64'd3) | ((dout_95&64'd1) << 64'd4) | ((dout_96&64'd1) << 64'd5) | ((dout_97&64'd1) << 64'd6) | ((dout_98&64'd1) << 64'd7) | ((dout_99&64'd1) << 64'd8) | ((dout_100&64'd1) << 64'd9) | ((dout_101&64'd1) << 64'd10) | ((dout_102&64'd1) << 64'd11) | ((dout_103&64'd1) << 64'd12);
endmodule

/***
* This code is a part of EvoApproxLib library (ehw.fit.vutbr.cz/approxlib) distributed under The MIT License.
* When used, please cite the following article(s): V. Mrazek, L. Sekanina, Z. Vasicek "Libraries of Approximate Circuits: Automated Design and Application in CNN Accelerators" IEEE Journal on Emerging and Selected Topics in Circuits and Systems, Vol 10, No 4, 2020 
* This file contains a circuit from a sub-set of pareto optimal circuits with respect to the pwr and mae parameters
***/
// MAE% = 0.20 %
// MAE = 8.2 
// WCE% = 0.46 %
// WCE = 19 
// WCRE% = 1800.00 %
// EP% = 96.97 %
// MRE% = 3.25 %
// MSE = 90 
// PDK45_PWR = 0.033 mW
// PDK45_AREA = 66.6 um2
// PDK45_DELAY = 0.60 ns

module approx_5PD(input wire [11:0] A, B, output wire [12:0] Y);
wire [63:0] dout_34;
wire [63:0] dout_35;
wire [63:0] dout_36;
wire [63:0] dout_37;
wire [63:0] dout_38;
wire [63:0] dout_39;
wire [63:0] dout_40;
wire [63:0] dout_41;
wire [63:0] dout_42;
wire [63:0] dout_43;
wire [63:0] dout_44;
wire [63:0] dout_45;
wire [63:0] dout_46;
wire [63:0] dout_47;
wire [63:0] dout_48;
wire [63:0] dout_55;
wire [63:0] dout_56;
wire [63:0] dout_57;
wire [63:0] dout_58;
wire [63:0] dout_59;
wire [63:0] dout_60;
wire [63:0] dout_61;
wire [63:0] dout_62;
wire [63:0] dout_63;
wire [63:0] dout_67;
wire [63:0] dout_69;
wire [63:0] dout_70;
wire [63:0] dout_71;
wire [63:0] dout_73;
wire [63:0] dout_74;
wire [63:0] dout_75;
wire [63:0] dout_78;
wire [63:0] dout_79;
wire [63:0] dout_84;
wire [63:0] dout_85;
wire [63:0] dout_86;
wire [63:0] dout_87;
wire [63:0] dout_88;
wire [63:0] dout_89;
wire [63:0] dout_90;
wire [63:0] dout_91;
wire [63:0] dout_96;
wire [63:0] dout_97;
wire [63:0] dout_98;
wire [63:0] dout_99;
wire [63:0] dout_100;
wire [63:0] dout_101;
wire [63:0] dout_102;
wire [63:0] dout_103;
assign dout_34 = ((A >> 64'd5)&64'd1)&((B >> 64'd5)&64'd1);
assign dout_35 = ((A >> 64'd5)&64'd1)^((B >> 64'd5)&64'd1);
assign dout_36 = ((A >> 64'd6)&64'd1)&((B >> 64'd6)&64'd1);
assign dout_37 = ((A >> 64'd6)&64'd1)^((B >> 64'd6)&64'd1);
assign dout_38 = ((A >> 64'd7)&64'd1)&((B >> 64'd7)&64'd1);
assign dout_39 = ((A >> 64'd7)&64'd1)^((B >> 64'd7)&64'd1);
assign dout_40 = ((A >> 64'd8)&64'd1)&((B >> 64'd8)&64'd1);
assign dout_41 = ((A >> 64'd8)&64'd1)^((B >> 64'd8)&64'd1);
assign dout_42 = ((A >> 64'd9)&64'd1)&((B >> 64'd9)&64'd1);
assign dout_43 = ((A >> 64'd9)&64'd1)^((B >> 64'd9)&64'd1);
assign dout_44 = ((A >> 64'd10)&64'd1)&((B >> 64'd10)&64'd1);
assign dout_45 = ((A >> 64'd10)&64'd1)^((B >> 64'd10)&64'd1);
assign dout_46 = ((A >> 64'd11)&64'd1)&((B >> 64'd11)&64'd1);
assign dout_47 = ((A >> 64'd11)&64'd1)^((B >> 64'd11)&64'd1);
assign dout_48 = ((A >> 64'd11)&64'd1)^((B >> 64'd11)&64'd1);
assign dout_55 = dout_37&dout_34;
assign dout_56 = dout_37&dout_35;
assign dout_57 = dout_36|dout_55;
assign dout_58 = dout_41&dout_38;
assign dout_59 = dout_41&dout_39;
assign dout_60 = dout_40|dout_58;
assign dout_61 = dout_45&dout_42;
assign dout_62 = dout_45&dout_43;
assign dout_63 = dout_44|dout_61;
assign dout_67 = dout_56&((A >> 64'd4)&64'd1);
assign dout_69 = dout_62&dout_60;
assign dout_70 = dout_62&dout_59;
assign dout_71 = dout_63|dout_69;
assign dout_73 = dout_57|dout_67;
assign dout_74 = dout_70&dout_73;
assign dout_75 = dout_71|dout_74;
assign dout_78 = dout_59&dout_73;
assign dout_79 = dout_60|dout_78;
assign dout_84 = dout_35&((A >> 64'd4)&64'd1);
assign dout_85 = dout_34|dout_84;
assign dout_86 = dout_39&dout_73;
assign dout_87 = dout_38|dout_86;
assign dout_88 = dout_43&dout_79;
assign dout_89 = dout_42|dout_88;
assign dout_90 = dout_47&dout_75;
assign dout_91 = dout_46|dout_90;
assign dout_96 = dout_35^((A >> 64'd4)&64'd1);
assign dout_97 = dout_37^dout_85;
assign dout_98 = dout_39^dout_73;
assign dout_99 = dout_41^dout_87;
assign dout_100 = dout_43^dout_79;
assign dout_101 = dout_45^dout_89;
assign dout_102 = dout_47^dout_75;
assign dout_103 = dout_48^dout_91;
assign Y = ((((B >> 64'd8)&64'd1)&64'd1) << 64'd0) | ((dout_79&64'd1) << 64'd1) | ((((B >> 64'd2)&64'd1)&64'd1) << 64'd2) | ((((A >> 64'd3)&64'd1)&64'd1) << 64'd3) | ((((B >> 64'd4)&64'd1)&64'd1) << 64'd4) | ((dout_96&64'd1) << 64'd5) | ((dout_97&64'd1) << 64'd6) | ((dout_98&64'd1) << 64'd7) | ((dout_99&64'd1) << 64'd8) | ((dout_100&64'd1) << 64'd9) | ((dout_101&64'd1) << 64'd10) | ((dout_102&64'd1) << 64'd11) | ((dout_103&64'd1) << 64'd12);
endmodule

/***
* This code is a part of EvoApproxLib library (ehw.fit.vutbr.cz/approxlib) distributed under The MIT License.
* When used, please cite the following article(s): V. Mrazek, L. Sekanina, Z. Vasicek "Libraries of Approximate Circuits: Automated Design and Application in CNN Accelerators" IEEE Journal on Emerging and Selected Topics in Circuits and Systems, Vol 10, No 4, 2020 
* This file contains a circuit from a sub-set of pareto optimal circuits with respect to the pwr and mae parameters
***/
// MAE% = 0.15 %
// MAE = 6.1 
// WCE% = 0.51 %
// WCE = 21 
// WCRE% = 2000.00 %
// EP% = 94.92 %
// MRE% = 2.53 %
// MSE = 57 
// PDK45_PWR = 0.033 mW
// PDK45_AREA = 68.0 um2
// PDK45_DELAY = 0.60 ns

module approx_5PN(input wire [11:0] A, B, output wire [12:0] Y);
wire [63:0] dout_34;
wire [63:0] dout_35;
wire [63:0] dout_36;
wire [63:0] dout_37;
wire [63:0] dout_38;
wire [63:0] dout_39;
wire [63:0] dout_40;
wire [63:0] dout_41;
wire [63:0] dout_42;
wire [63:0] dout_43;
wire [63:0] dout_44;
wire [63:0] dout_45;
wire [63:0] dout_46;
wire [63:0] dout_47;
wire [63:0] dout_48;
wire [63:0] dout_55;
wire [63:0] dout_56;
wire [63:0] dout_57;
wire [63:0] dout_58;
wire [63:0] dout_59;
wire [63:0] dout_60;
wire [63:0] dout_61;
wire [63:0] dout_62;
wire [63:0] dout_63;
wire [63:0] dout_66;
wire [63:0] dout_68;
wire [63:0] dout_69;
wire [63:0] dout_70;
wire [63:0] dout_71;
wire [63:0] dout_74;
wire [63:0] dout_75;
wire [63:0] dout_78;
wire [63:0] dout_79;
wire [63:0] dout_84;
wire [63:0] dout_85;
wire [63:0] dout_86;
wire [63:0] dout_87;
wire [63:0] dout_88;
wire [63:0] dout_89;
wire [63:0] dout_90;
wire [63:0] dout_91;
wire [63:0] dout_95;
wire [63:0] dout_96;
wire [63:0] dout_97;
wire [63:0] dout_98;
wire [63:0] dout_99;
wire [63:0] dout_100;
wire [63:0] dout_101;
wire [63:0] dout_102;
wire [63:0] dout_103;
assign dout_34 = ((A >> 64'd5)&64'd1)&((B >> 64'd5)&64'd1);
assign dout_35 = ((A >> 64'd5)&64'd1)^((B >> 64'd5)&64'd1);
assign dout_36 = ((A >> 64'd6)&64'd1)&((B >> 64'd6)&64'd1);
assign dout_37 = ((A >> 64'd6)&64'd1)^((B >> 64'd6)&64'd1);
assign dout_38 = ((A >> 64'd7)&64'd1)&((B >> 64'd7)&64'd1);
assign dout_39 = ((A >> 64'd7)&64'd1)^((B >> 64'd7)&64'd1);
assign dout_40 = ((A >> 64'd8)&64'd1)&((B >> 64'd8)&64'd1);
assign dout_41 = ((A >> 64'd8)&64'd1)^((B >> 64'd8)&64'd1);
assign dout_42 = ((A >> 64'd9)&64'd1)&((B >> 64'd9)&64'd1);
assign dout_43 = ((A >> 64'd9)&64'd1)^((B >> 64'd9)&64'd1);
assign dout_44 = ((A >> 64'd10)&64'd1)&((B >> 64'd10)&64'd1);
assign dout_45 = ((A >> 64'd10)&64'd1)^((B >> 64'd10)&64'd1);
assign dout_46 = ((A >> 64'd11)&64'd1)&((B >> 64'd11)&64'd1);
assign dout_47 = ((A >> 64'd11)&64'd1)^((B >> 64'd11)&64'd1);
assign dout_48 = ((A >> 64'd11)&64'd1)^((B >> 64'd11)&64'd1);
assign dout_55 = dout_37&dout_34;
assign dout_56 = dout_37&dout_35;
assign dout_57 = dout_36|dout_55;
assign dout_58 = dout_41&dout_38;
assign dout_59 = dout_41&dout_39;
assign dout_60 = dout_40|dout_58;
assign dout_61 = dout_45&dout_42;
assign dout_62 = dout_45&dout_43;
assign dout_63 = dout_44|dout_61;
assign dout_66 = dout_56&((A >> 64'd4)&64'd1);
assign dout_68 = dout_57|dout_66;
assign dout_69 = dout_62&dout_60;
assign dout_70 = dout_62&dout_59;
assign dout_71 = dout_63|dout_69;
assign dout_74 = dout_70&dout_68;
assign dout_75 = dout_71|dout_74;
assign dout_78 = dout_59&dout_68;
assign dout_79 = dout_60|dout_78;
assign dout_84 = dout_35&((A >> 64'd4)&64'd1);
assign dout_85 = dout_34|dout_84;
assign dout_86 = dout_39&dout_68;
assign dout_87 = dout_38|dout_86;
assign dout_88 = dout_43&dout_79;
assign dout_89 = dout_42|dout_88;
assign dout_90 = dout_47&dout_75;
assign dout_91 = dout_46|dout_90;
assign dout_95 = ((A >> 64'd4)&64'd1)^64'd18446744073709551615;
assign dout_96 = dout_35^((A >> 64'd4)&64'd1);
assign dout_97 = dout_37^dout_85;
assign dout_98 = dout_39^dout_68;
assign dout_99 = dout_41^dout_87;
assign dout_100 = dout_43^dout_79;
assign dout_101 = dout_45^dout_89;
assign dout_102 = dout_47^dout_75;
assign dout_103 = dout_48^dout_91;
assign Y = ((dout_89&64'd1) << 64'd0) | ((((B >> 64'd4)&64'd1)&64'd1) << 64'd1) | ((dout_75&64'd1) << 64'd2) | ((((B >> 64'd4)&64'd1)&64'd1) << 64'd3) | ((dout_95&64'd1) << 64'd4) | ((dout_96&64'd1) << 64'd5) | ((dout_97&64'd1) << 64'd6) | ((dout_98&64'd1) << 64'd7) | ((dout_99&64'd1) << 64'd8) | ((dout_100&64'd1) << 64'd9) | ((dout_101&64'd1) << 64'd10) | ((dout_102&64'd1) << 64'd11) | ((dout_103&64'd1) << 64'd12);
endmodule

/***
* This code is a part of EvoApproxLib library (ehw.fit.vutbr.cz/approxlib) distributed under The MIT License.
* When used, please cite the following article(s): V. Mrazek, L. Sekanina, Z. Vasicek "Libraries of Approximate Circuits: Automated Design and Application in CNN Accelerators" IEEE Journal on Emerging and Selected Topics in Circuits and Systems, Vol 10, No 4, 2020 
* This file contains a circuit from a sub-set of pareto optimal circuits with respect to the pwr and mae parameters
***/
// MAE% = 0.012 %
// MAE = 0.5 
// WCE% = 0.024 %
// WCE = 1.0 
// WCRE% = 100.00 %
// EP% = 50.00 %
// MRE% = 0.21 %
// MSE = 0.5 
// PDK45_PWR = 0.052 mW
// PDK45_AREA = 102.3 um2
// PDK45_DELAY = 0.92 ns

module approx_5QC(input wire [11:0] A, B, output wire [12:0] Y);
wire [63:0] dout_26;
wire [63:0] dout_27;
wire [63:0] dout_28;
wire [63:0] dout_29;
wire [63:0] dout_30;
wire [63:0] dout_31;
wire [63:0] dout_32;
wire [63:0] dout_33;
wire [63:0] dout_34;
wire [63:0] dout_35;
wire [63:0] dout_36;
wire [63:0] dout_37;
wire [63:0] dout_38;
wire [63:0] dout_39;
wire [63:0] dout_40;
wire [63:0] dout_41;
wire [63:0] dout_42;
wire [63:0] dout_43;
wire [63:0] dout_44;
wire [63:0] dout_45;
wire [63:0] dout_46;
wire [63:0] dout_47;
wire [63:0] dout_48;
wire [63:0] dout_49;
wire [63:0] dout_50;
wire [63:0] dout_51;
wire [63:0] dout_52;
wire [63:0] dout_53;
wire [63:0] dout_54;
wire [63:0] dout_55;
wire [63:0] dout_56;
wire [63:0] dout_57;
wire [63:0] dout_58;
wire [63:0] dout_59;
wire [63:0] dout_60;
wire [63:0] dout_61;
wire [63:0] dout_62;
wire [63:0] dout_63;
wire [63:0] dout_64;
wire [63:0] dout_65;
wire [63:0] dout_66;
wire [63:0] dout_67;
wire [63:0] dout_68;
wire [63:0] dout_69;
wire [63:0] dout_70;
wire [63:0] dout_71;
wire [63:0] dout_72;
wire [63:0] dout_73;
wire [63:0] dout_74;
wire [63:0] dout_75;
wire [63:0] dout_76;
wire [63:0] dout_77;
wire [63:0] dout_78;
wire [63:0] dout_79;
wire [63:0] dout_80;
wire [63:0] dout_81;
wire [63:0] dout_82;
wire [63:0] dout_83;
wire [63:0] dout_84;
wire [63:0] dout_85;
wire [63:0] dout_86;
wire [63:0] dout_87;
wire [63:0] dout_88;
wire [63:0] dout_89;
wire [63:0] dout_90;
wire [63:0] dout_91;
wire [63:0] dout_92;
wire [63:0] dout_93;
wire [63:0] dout_94;
wire [63:0] dout_95;
wire [63:0] dout_96;
wire [63:0] dout_97;
wire [63:0] dout_98;
wire [63:0] dout_99;
wire [63:0] dout_100;
wire [63:0] dout_101;
wire [63:0] dout_102;
wire [63:0] dout_103;
wire [63:0] dout_104;
wire [63:0] dout_105;
wire [63:0] dout_106;
wire [63:0] dout_107;
wire [63:0] dout_108;
wire [63:0] dout_109;
wire [63:0] dout_110;
wire [63:0] dout_111;
wire [63:0] dout_112;
assign dout_26 = ((A >> 64'd1)&64'd1)&((B >> 64'd1)&64'd1);
assign dout_27 = ((A >> 64'd1)&64'd1)^((B >> 64'd1)&64'd1);
assign dout_28 = ((A >> 64'd2)&64'd1)&((B >> 64'd2)&64'd1);
assign dout_29 = ((A >> 64'd2)&64'd1)^((B >> 64'd2)&64'd1);
assign dout_30 = ((A >> 64'd3)&64'd1)&((B >> 64'd3)&64'd1);
assign dout_31 = ((A >> 64'd3)&64'd1)^((B >> 64'd3)&64'd1);
assign dout_32 = ((A >> 64'd4)&64'd1)&((B >> 64'd4)&64'd1);
assign dout_33 = ((A >> 64'd4)&64'd1)^((B >> 64'd4)&64'd1);
assign dout_34 = ((A >> 64'd5)&64'd1)&((B >> 64'd5)&64'd1);
assign dout_35 = ((A >> 64'd5)&64'd1)^((B >> 64'd5)&64'd1);
assign dout_36 = ((A >> 64'd6)&64'd1)&((B >> 64'd6)&64'd1);
assign dout_37 = ((A >> 64'd6)&64'd1)^((B >> 64'd6)&64'd1);
assign dout_38 = ((A >> 64'd7)&64'd1)&((B >> 64'd7)&64'd1);
assign dout_39 = ((A >> 64'd7)&64'd1)^((B >> 64'd7)&64'd1);
assign dout_40 = ((A >> 64'd8)&64'd1)&((B >> 64'd8)&64'd1);
assign dout_41 = ((A >> 64'd8)&64'd1)^((B >> 64'd8)&64'd1);
assign dout_42 = ((A >> 64'd9)&64'd1)&((B >> 64'd9)&64'd1);
assign dout_43 = ((A >> 64'd9)&64'd1)^((B >> 64'd9)&64'd1);
assign dout_44 = ((A >> 64'd10)&64'd1)&((B >> 64'd10)&64'd1);
assign dout_45 = ((A >> 64'd10)&64'd1)^((B >> 64'd10)&64'd1);
assign dout_46 = ((A >> 64'd11)&64'd1)&((B >> 64'd11)&64'd1);
assign dout_47 = ((A >> 64'd11)&64'd1)^((B >> 64'd11)&64'd1);
assign dout_48 = ((A >> 64'd11)&64'd1)^((B >> 64'd11)&64'd1);
assign dout_49 = dout_29&dout_26;
assign dout_50 = dout_29&dout_27;
assign dout_51 = dout_28|dout_49;
assign dout_52 = dout_33&dout_30;
assign dout_53 = dout_33&dout_31;
assign dout_54 = dout_32|dout_52;
assign dout_55 = dout_37&dout_34;
assign dout_56 = dout_37&dout_35;
assign dout_57 = dout_36|dout_55;
assign dout_58 = dout_41&dout_38;
assign dout_59 = dout_41&dout_39;
assign dout_60 = dout_40|dout_58;
assign dout_61 = dout_45&dout_42;
assign dout_62 = dout_45&dout_43;
assign dout_63 = dout_44|dout_61;
assign dout_64 = dout_27&((A >> 64'd0)&64'd1);
assign dout_65 = dout_26|dout_64;
assign dout_66 = dout_50&((A >> 64'd0)&64'd1);
assign dout_67 = dout_51|dout_66;
assign dout_68 = dout_35&dout_54;
assign dout_69 = dout_35&dout_53;
assign dout_70 = dout_34|dout_68;
assign dout_71 = dout_56&dout_54;
assign dout_72 = dout_56&dout_53;
assign dout_73 = dout_57|dout_71;
assign dout_74 = dout_43&dout_60;
assign dout_75 = dout_43&dout_59;
assign dout_76 = dout_42|dout_74;
assign dout_77 = dout_62&dout_60;
assign dout_78 = dout_62&dout_59;
assign dout_79 = dout_63|dout_77;
assign dout_80 = dout_31&dout_67;
assign dout_81 = dout_30|dout_80;
assign dout_82 = dout_53&dout_67;
assign dout_83 = dout_54|dout_82;
assign dout_84 = dout_69&dout_67;
assign dout_85 = dout_70|dout_84;
assign dout_86 = dout_72&dout_67;
assign dout_87 = dout_73|dout_86;
assign dout_88 = dout_47&dout_79;
assign dout_89 = dout_47&dout_78;
assign dout_90 = dout_46|dout_88;
assign dout_91 = dout_39&dout_87;
assign dout_92 = dout_38|dout_91;
assign dout_93 = dout_59&dout_87;
assign dout_94 = dout_60|dout_93;
assign dout_95 = dout_75&dout_87;
assign dout_96 = dout_76|dout_95;
assign dout_97 = dout_78&dout_87;
assign dout_98 = dout_79|dout_97;
assign dout_99 = dout_89&dout_87;
assign dout_100 = dout_90|dout_99;
assign dout_101 = dout_27^((A >> 64'd0)&64'd1);
assign dout_102 = dout_29^dout_65;
assign dout_103 = dout_31^dout_67;
assign dout_104 = dout_33^dout_81;
assign dout_105 = dout_35^dout_83;
assign dout_106 = dout_37^dout_85;
assign dout_107 = dout_39^dout_87;
assign dout_108 = dout_41^dout_92;
assign dout_109 = dout_43^dout_94;
assign dout_110 = dout_45^dout_96;
assign dout_111 = dout_47^dout_98;
assign dout_112 = dout_48^dout_100;
assign Y = ((64'd0&64'd1) << 64'd0) | ((dout_101&64'd1) << 64'd1) | ((dout_102&64'd1) << 64'd2) | ((dout_103&64'd1) << 64'd3) | ((dout_104&64'd1) << 64'd4) | ((dout_105&64'd1) << 64'd5) | ((dout_106&64'd1) << 64'd6) | ((dout_107&64'd1) << 64'd7) | ((dout_108&64'd1) << 64'd8) | ((dout_109&64'd1) << 64'd9) | ((dout_110&64'd1) << 64'd10) | ((dout_111&64'd1) << 64'd11) | ((dout_112&64'd1) << 64'd12);
endmodule

/***
* This code is a part of EvoApproxLib library (ehw.fit.vutbr.cz/approxlib) distributed under The MIT License.
* When used, please cite the following article(s): V. Mrazek, L. Sekanina, Z. Vasicek "Libraries of Approximate Circuits: Automated Design and Application in CNN Accelerators" IEEE Journal on Emerging and Selected Topics in Circuits and Systems, Vol 10, No 4, 2020 
* This file contains a circuit from a sub-set of pareto optimal circuits with respect to the pwr and mae parameters
***/
// MAE% = 0.0049 %
// MAE = 0.2 
// WCE% = 0.024 %
// WCE = 1.0 
// WCRE% = 50.00 %
// EP% = 25.00 %
// MRE% = 0.088 %
// MSE = 0.2 
// PDK45_PWR = 0.053 mW
// PDK45_AREA = 106.1 um2
// PDK45_DELAY = 0.95 ns

module approx_5QT(input wire [11:0] A, B, output wire [12:0] Y);
wire [63:0] dout_24;
wire [63:0] dout_25;
wire [63:0] dout_26;
wire [63:0] dout_27;
wire [63:0] dout_28;
wire [63:0] dout_29;
wire [63:0] dout_30;
wire [63:0] dout_31;
wire [63:0] dout_32;
wire [63:0] dout_33;
wire [63:0] dout_34;
wire [63:0] dout_35;
wire [63:0] dout_36;
wire [63:0] dout_37;
wire [63:0] dout_38;
wire [63:0] dout_39;
wire [63:0] dout_40;
wire [63:0] dout_41;
wire [63:0] dout_42;
wire [63:0] dout_43;
wire [63:0] dout_44;
wire [63:0] dout_45;
wire [63:0] dout_46;
wire [63:0] dout_47;
wire [63:0] dout_48;
wire [63:0] dout_49;
wire [63:0] dout_50;
wire [63:0] dout_51;
wire [63:0] dout_52;
wire [63:0] dout_53;
wire [63:0] dout_54;
wire [63:0] dout_55;
wire [63:0] dout_56;
wire [63:0] dout_57;
wire [63:0] dout_58;
wire [63:0] dout_59;
wire [63:0] dout_60;
wire [63:0] dout_61;
wire [63:0] dout_62;
wire [63:0] dout_63;
wire [63:0] dout_64;
wire [63:0] dout_65;
wire [63:0] dout_66;
wire [63:0] dout_67;
wire [63:0] dout_68;
wire [63:0] dout_69;
wire [63:0] dout_70;
wire [63:0] dout_71;
wire [63:0] dout_72;
wire [63:0] dout_73;
wire [63:0] dout_74;
wire [63:0] dout_75;
wire [63:0] dout_76;
wire [63:0] dout_77;
wire [63:0] dout_78;
wire [63:0] dout_79;
wire [63:0] dout_80;
wire [63:0] dout_81;
wire [63:0] dout_82;
wire [63:0] dout_83;
wire [63:0] dout_84;
wire [63:0] dout_85;
wire [63:0] dout_86;
wire [63:0] dout_87;
wire [63:0] dout_88;
wire [63:0] dout_89;
wire [63:0] dout_90;
wire [63:0] dout_91;
wire [63:0] dout_92;
wire [63:0] dout_93;
wire [63:0] dout_94;
wire [63:0] dout_95;
wire [63:0] dout_96;
wire [63:0] dout_97;
wire [63:0] dout_98;
wire [63:0] dout_99;
wire [63:0] dout_100;
wire [63:0] dout_101;
wire [63:0] dout_102;
wire [63:0] dout_103;
wire [63:0] dout_104;
wire [63:0] dout_105;
wire [63:0] dout_106;
wire [63:0] dout_107;
wire [63:0] dout_108;
wire [63:0] dout_109;
wire [63:0] dout_110;
wire [63:0] dout_111;
wire [63:0] dout_112;
assign dout_24 = ((A >> 64'd0)&64'd1)&((B >> 64'd0)&64'd1);
assign dout_25 = (((A >> 64'd0)&64'd1)&((B >> 64'd0)&64'd1))^64'd18446744073709551615;
assign dout_26 = ((A >> 64'd1)&64'd1)&((B >> 64'd1)&64'd1);
assign dout_27 = ((A >> 64'd1)&64'd1)^((B >> 64'd1)&64'd1);
assign dout_28 = ((A >> 64'd2)&64'd1)&((B >> 64'd2)&64'd1);
assign dout_29 = ((A >> 64'd2)&64'd1)^((B >> 64'd2)&64'd1);
assign dout_30 = ((A >> 64'd3)&64'd1)&((B >> 64'd3)&64'd1);
assign dout_31 = ((A >> 64'd3)&64'd1)^((B >> 64'd3)&64'd1);
assign dout_32 = ((A >> 64'd4)&64'd1)&((B >> 64'd4)&64'd1);
assign dout_33 = ((A >> 64'd4)&64'd1)^((B >> 64'd4)&64'd1);
assign dout_34 = ((A >> 64'd5)&64'd1)&((B >> 64'd5)&64'd1);
assign dout_35 = ((A >> 64'd5)&64'd1)^((B >> 64'd5)&64'd1);
assign dout_36 = ((A >> 64'd6)&64'd1)&((B >> 64'd6)&64'd1);
assign dout_37 = ((A >> 64'd6)&64'd1)^((B >> 64'd6)&64'd1);
assign dout_38 = ((A >> 64'd7)&64'd1)&((B >> 64'd7)&64'd1);
assign dout_39 = ((A >> 64'd7)&64'd1)^((B >> 64'd7)&64'd1);
assign dout_40 = ((A >> 64'd8)&64'd1)&((B >> 64'd8)&64'd1);
assign dout_41 = ((A >> 64'd8)&64'd1)^((B >> 64'd8)&64'd1);
assign dout_42 = ((A >> 64'd9)&64'd1)&((B >> 64'd9)&64'd1);
assign dout_43 = ((A >> 64'd9)&64'd1)^((B >> 64'd9)&64'd1);
assign dout_44 = ((A >> 64'd10)&64'd1)&((B >> 64'd10)&64'd1);
assign dout_45 = ((A >> 64'd10)&64'd1)^((B >> 64'd10)&64'd1);
assign dout_46 = ((A >> 64'd11)&64'd1)&((B >> 64'd11)&64'd1);
assign dout_47 = ((A >> 64'd11)&64'd1)^((B >> 64'd11)&64'd1);
assign dout_48 = ((A >> 64'd11)&64'd1)^((B >> 64'd11)&64'd1);
assign dout_49 = dout_29&dout_26;
assign dout_50 = dout_29&dout_27;
assign dout_51 = dout_28|dout_49;
assign dout_52 = dout_33&dout_30;
assign dout_53 = dout_33&dout_31;
assign dout_54 = dout_32|dout_52;
assign dout_55 = dout_37&dout_34;
assign dout_56 = dout_37&dout_35;
assign dout_57 = dout_36|dout_55;
assign dout_58 = dout_41&dout_38;
assign dout_59 = dout_41&dout_39;
assign dout_60 = dout_40|dout_58;
assign dout_61 = dout_45&dout_42;
assign dout_62 = dout_45&dout_43;
assign dout_63 = dout_44|dout_61;
assign dout_64 = dout_27&dout_24;
assign dout_65 = dout_26|dout_64;
assign dout_66 = dout_50&dout_24;
assign dout_67 = dout_51|dout_66;
assign dout_68 = dout_35&dout_54;
assign dout_69 = dout_35&dout_53;
assign dout_70 = dout_34|dout_68;
assign dout_71 = dout_56&dout_54;
assign dout_72 = dout_56&dout_53;
assign dout_73 = dout_57|dout_71;
assign dout_74 = dout_43&dout_60;
assign dout_75 = dout_43&dout_59;
assign dout_76 = dout_42|dout_74;
assign dout_77 = dout_62&dout_60;
assign dout_78 = dout_62&dout_59;
assign dout_79 = dout_63|dout_77;
assign dout_80 = dout_31&dout_67;
assign dout_81 = dout_30|dout_80;
assign dout_82 = dout_53&dout_67;
assign dout_83 = dout_54|dout_82;
assign dout_84 = dout_69&dout_67;
assign dout_85 = dout_70|dout_84;
assign dout_86 = dout_72&dout_67;
assign dout_87 = dout_73|dout_86;
assign dout_88 = dout_47&dout_79;
assign dout_89 = dout_47&dout_78;
assign dout_90 = dout_46|dout_88;
assign dout_91 = dout_39&dout_87;
assign dout_92 = dout_38|dout_91;
assign dout_93 = dout_59&dout_87;
assign dout_94 = dout_60|dout_93;
assign dout_95 = dout_75&dout_87;
assign dout_96 = dout_76|dout_95;
assign dout_97 = dout_78&dout_87;
assign dout_98 = dout_79|dout_97;
assign dout_99 = dout_89&dout_87;
assign dout_100 = dout_90|dout_99;
assign dout_101 = dout_27^dout_24;
assign dout_102 = dout_29^dout_65;
assign dout_103 = dout_31^dout_67;
assign dout_104 = dout_33^dout_81;
assign dout_105 = dout_35^dout_83;
assign dout_106 = dout_37^dout_85;
assign dout_107 = dout_39^dout_87;
assign dout_108 = dout_41^dout_92;
assign dout_109 = dout_43^dout_94;
assign dout_110 = dout_45^dout_96;
assign dout_111 = dout_47^dout_98;
assign dout_112 = dout_48^dout_100;
assign Y = ((dout_25&64'd1) << 64'd0) | ((dout_101&64'd1) << 64'd1) | ((dout_102&64'd1) << 64'd2) | ((dout_103&64'd1) << 64'd3) | ((dout_104&64'd1) << 64'd4) | ((dout_105&64'd1) << 64'd5) | ((dout_106&64'd1) << 64'd6) | ((dout_107&64'd1) << 64'd7) | ((dout_108&64'd1) << 64'd8) | ((dout_109&64'd1) << 64'd9) | ((dout_110&64'd1) << 64'd10) | ((dout_111&64'd1) << 64'd11) | ((dout_112&64'd1) << 64'd12);
endmodule

/***
* This code is a part of EvoApproxLib library (ehw.fit.vutbr.cz/approxlib) distributed under The MIT License.
* When used, please cite the following article(s): V. Mrazek, L. Sekanina, Z. Vasicek "Libraries of Approximate Circuits: Automated Design and Application in CNN Accelerators" IEEE Journal on Emerging and Selected Topics in Circuits and Systems, Vol 10, No 4, 2020 
* This file contains a circuit from a sub-set of pareto optimal circuits with respect to the pwr and mae parameters
***/
// MAE% = 0.037 %
// MAE = 1.5 
// WCE% = 0.098 %
// WCE = 4.0 
// WCRE% = 400.00 %
// EP% = 81.25 %
// MRE% = 0.58 %
// MSE = 3.5 
// PDK45_PWR = 0.043 mW
// PDK45_AREA = 85.9 um2
// PDK45_DELAY = 0.76 ns

module approx_5RP(input wire [11:0] A, B, output wire [12:0] Y);
wire [63:0] dout_30;
wire [63:0] dout_31;
wire [63:0] dout_32;
wire [63:0] dout_33;
wire [63:0] dout_34;
wire [63:0] dout_35;
wire [63:0] dout_36;
wire [63:0] dout_37;
wire [63:0] dout_38;
wire [63:0] dout_39;
wire [63:0] dout_40;
wire [63:0] dout_41;
wire [63:0] dout_42;
wire [63:0] dout_43;
wire [63:0] dout_44;
wire [63:0] dout_45;
wire [63:0] dout_46;
wire [63:0] dout_47;
wire [63:0] dout_48;
wire [63:0] dout_52;
wire [63:0] dout_53;
wire [63:0] dout_54;
wire [63:0] dout_55;
wire [63:0] dout_56;
wire [63:0] dout_57;
wire [63:0] dout_58;
wire [63:0] dout_59;
wire [63:0] dout_60;
wire [63:0] dout_61;
wire [63:0] dout_62;
wire [63:0] dout_63;
wire [63:0] dout_66;
wire [63:0] dout_67;
wire [63:0] dout_68;
wire [63:0] dout_69;
wire [63:0] dout_70;
wire [63:0] dout_71;
wire [63:0] dout_72;
wire [63:0] dout_73;
wire [63:0] dout_74;
wire [63:0] dout_75;
wire [63:0] dout_76;
wire [63:0] dout_77;
wire [63:0] dout_78;
wire [63:0] dout_79;
wire [63:0] dout_82;
wire [63:0] dout_83;
wire [63:0] dout_84;
wire [63:0] dout_85;
wire [63:0] dout_86;
wire [63:0] dout_87;
wire [63:0] dout_88;
wire [63:0] dout_89;
wire [63:0] dout_90;
wire [63:0] dout_91;
wire [63:0] dout_93;
wire [63:0] dout_94;
wire [63:0] dout_95;
wire [63:0] dout_96;
wire [63:0] dout_97;
wire [63:0] dout_98;
wire [63:0] dout_99;
wire [63:0] dout_100;
wire [63:0] dout_101;
wire [63:0] dout_102;
wire [63:0] dout_103;
assign dout_30 = ((A >> 64'd3)&64'd1)&((B >> 64'd3)&64'd1);
assign dout_31 = ((A >> 64'd3)&64'd1)^((B >> 64'd3)&64'd1);
assign dout_32 = ((A >> 64'd4)&64'd1)&((B >> 64'd4)&64'd1);
assign dout_33 = ((A >> 64'd4)&64'd1)^((B >> 64'd4)&64'd1);
assign dout_34 = ((A >> 64'd5)&64'd1)&((B >> 64'd5)&64'd1);
assign dout_35 = ((A >> 64'd5)&64'd1)^((B >> 64'd5)&64'd1);
assign dout_36 = ((A >> 64'd6)&64'd1)&((B >> 64'd6)&64'd1);
assign dout_37 = ((A >> 64'd6)&64'd1)^((B >> 64'd6)&64'd1);
assign dout_38 = ((A >> 64'd7)&64'd1)&((B >> 64'd7)&64'd1);
assign dout_39 = ((A >> 64'd7)&64'd1)^((B >> 64'd7)&64'd1);
assign dout_40 = ((A >> 64'd8)&64'd1)&((B >> 64'd8)&64'd1);
assign dout_41 = ((A >> 64'd8)&64'd1)^((B >> 64'd8)&64'd1);
assign dout_42 = ((A >> 64'd9)&64'd1)&((B >> 64'd9)&64'd1);
assign dout_43 = ((A >> 64'd9)&64'd1)^((B >> 64'd9)&64'd1);
assign dout_44 = ((A >> 64'd10)&64'd1)&((B >> 64'd10)&64'd1);
assign dout_45 = ((A >> 64'd10)&64'd1)^((B >> 64'd10)&64'd1);
assign dout_46 = ((A >> 64'd11)&64'd1)&((B >> 64'd11)&64'd1);
assign dout_47 = ((A >> 64'd11)&64'd1)^((B >> 64'd11)&64'd1);
assign dout_48 = ((A >> 64'd11)&64'd1)^((B >> 64'd11)&64'd1);
assign dout_52 = dout_33&dout_30;
assign dout_53 = dout_33&dout_31;
assign dout_54 = dout_32|dout_52;
assign dout_55 = dout_37&dout_34;
assign dout_56 = dout_37&dout_35;
assign dout_57 = dout_36|dout_55;
assign dout_58 = dout_41&dout_38;
assign dout_59 = dout_41&dout_39;
assign dout_60 = dout_40|dout_58;
assign dout_61 = dout_45&dout_42;
assign dout_62 = dout_45&dout_43;
assign dout_63 = dout_44|dout_61;
assign dout_66 = dout_56&dout_54;
assign dout_67 = dout_56&dout_53;
assign dout_68 = dout_57|dout_66;
assign dout_69 = dout_62&dout_60;
assign dout_70 = dout_62&dout_59;
assign dout_71 = dout_63|dout_69;
assign dout_72 = dout_67&((A >> 64'd2)&64'd1);
assign dout_73 = dout_68|dout_72;
assign dout_74 = dout_70&dout_73;
assign dout_75 = dout_71|dout_74;
assign dout_76 = dout_53&((A >> 64'd2)&64'd1);
assign dout_77 = dout_54|dout_76;
assign dout_78 = dout_59&dout_73;
assign dout_79 = dout_60|dout_78;
assign dout_82 = dout_31&((A >> 64'd2)&64'd1);
assign dout_83 = dout_30|dout_82;
assign dout_84 = dout_35&dout_77;
assign dout_85 = dout_34|dout_84;
assign dout_86 = dout_39&dout_73;
assign dout_87 = dout_38|dout_86;
assign dout_88 = dout_43&dout_79;
assign dout_89 = dout_42|dout_88;
assign dout_90 = dout_47&dout_75;
assign dout_91 = dout_46|dout_90;
assign dout_93 = ((A >> 64'd2)&64'd1)^64'd18446744073709551615;
assign dout_94 = dout_31^((A >> 64'd2)&64'd1);
assign dout_95 = dout_33^dout_83;
assign dout_96 = dout_35^dout_77;
assign dout_97 = dout_37^dout_85;
assign dout_98 = dout_39^dout_73;
assign dout_99 = dout_41^dout_87;
assign dout_100 = dout_43^dout_79;
assign dout_101 = dout_45^dout_89;
assign dout_102 = dout_47^dout_75;
assign dout_103 = dout_48^dout_91;
assign Y = ((((A >> 64'd0)&64'd1)&64'd1) << 64'd0) | ((((B >> 64'd2)&64'd1)&64'd1) << 64'd1) | ((dout_93&64'd1) << 64'd2) | ((dout_94&64'd1) << 64'd3) | ((dout_95&64'd1) << 64'd4) | ((dout_96&64'd1) << 64'd5) | ((dout_97&64'd1) << 64'd6) | ((dout_98&64'd1) << 64'd7) | ((dout_99&64'd1) << 64'd8) | ((dout_100&64'd1) << 64'd9) | ((dout_101&64'd1) << 64'd10) | ((dout_102&64'd1) << 64'd11) | ((dout_103&64'd1) << 64'd12);
endmodule

/***
* This code is a part of EvoApproxLib library (ehw.fit.vutbr.cz/approxlib) distributed under The MIT License.
* When used, please cite the following article(s): V. Mrazek, L. Sekanina, Z. Vasicek "Libraries of Approximate Circuits: Automated Design and Application in CNN Accelerators" IEEE Journal on Emerging and Selected Topics in Circuits and Systems, Vol 10, No 4, 2020 
* This file contains a circuit from a sub-set of pareto optimal circuits with respect to the pwr and mae parameters
***/
// MAE% = 0.32 %
// MAE = 13 
// WCE% = 1.15 %
// WCE = 47 
// WCRE% = 3300.00 %
// EP% = 97.66 %
// MRE% = 4.19 %
// MSE = 260 
// PDK45_PWR = 0.028 mW
// PDK45_AREA = 61.5 um2
// PDK45_DELAY = 0.51 ns

module approx_5SB(input wire [11:0] A, B, output wire [12:0] Y);
wire [63:0] dout_49;
wire [63:0] dout_51;
wire [63:0] dout_52;
wire [63:0] dout_53;
wire [63:0] dout_54;
wire [63:0] dout_55;
wire [63:0] dout_56;
wire [63:0] dout_57;
wire [63:0] dout_58;
wire [63:0] dout_59;
wire [63:0] dout_60;
wire [63:0] dout_61;
wire [63:0] dout_62;
wire [63:0] dout_63;
wire [63:0] dout_64;
wire [63:0] dout_65;
wire [63:0] dout_66;
wire [63:0] dout_67;
wire [63:0] dout_68;
wire [63:0] dout_69;
wire [63:0] dout_70;
wire [63:0] dout_71;
wire [63:0] dout_72;
wire [63:0] dout_73;
wire [63:0] dout_74;
wire [63:0] dout_75;
wire [63:0] dout_76;
wire [63:0] dout_77;
wire [63:0] dout_78;
wire [63:0] dout_79;
wire [63:0] dout_80;
wire [63:0] dout_81;
wire [63:0] dout_82;
assign dout_49 = ((B >> 64'd5)&64'd1)^64'd18446744073709551615;
assign dout_51 = ((A >> 64'd6)&64'd1)^((B >> 64'd6)&64'd1);
assign dout_52 = ((A >> 64'd6)&64'd1)&((B >> 64'd6)&64'd1);
assign dout_53 = dout_51&((B >> 64'd5)&64'd1);
assign dout_54 = dout_51^((B >> 64'd5)&64'd1);
assign dout_55 = dout_52|dout_53;
assign dout_56 = ((A >> 64'd7)&64'd1)^((B >> 64'd7)&64'd1);
assign dout_57 = ((A >> 64'd7)&64'd1)&((B >> 64'd7)&64'd1);
assign dout_58 = dout_56&dout_55;
assign dout_59 = dout_56^dout_55;
assign dout_60 = dout_57|dout_58;
assign dout_61 = ((A >> 64'd8)&64'd1)^((B >> 64'd8)&64'd1);
assign dout_62 = ((A >> 64'd8)&64'd1)&((B >> 64'd8)&64'd1);
assign dout_63 = dout_61&dout_60;
assign dout_64 = dout_61^dout_60;
assign dout_65 = dout_62|dout_63;
assign dout_66 = ((A >> 64'd9)&64'd1)^((B >> 64'd9)&64'd1);
assign dout_67 = ((A >> 64'd9)&64'd1)&((B >> 64'd9)&64'd1);
assign dout_68 = dout_66&dout_65;
assign dout_69 = dout_66^dout_65;
assign dout_70 = dout_67|dout_68;
assign dout_71 = ((A >> 64'd10)&64'd1)^((B >> 64'd10)&64'd1);
assign dout_72 = ((A >> 64'd10)&64'd1)&((B >> 64'd10)&64'd1);
assign dout_73 = dout_71&dout_70;
assign dout_74 = dout_71^dout_70;
assign dout_75 = dout_72|dout_73;
assign dout_76 = ((A >> 64'd11)&64'd1)^((B >> 64'd11)&64'd1);
assign dout_77 = ((A >> 64'd11)&64'd1)&((B >> 64'd11)&64'd1);
assign dout_78 = dout_76&dout_75;
assign dout_79 = dout_76^dout_75;
assign dout_80 = dout_77|dout_78;
assign dout_81 = ((A >> 64'd11)&64'd1)^((B >> 64'd11)&64'd1);
assign dout_82 = dout_81^dout_80;
assign Y = ((dout_79&64'd1) << 64'd0) | ((dout_72&64'd1) << 64'd1) | ((((B >> 64'd5)&64'd1)&64'd1) << 64'd2) | ((dout_79&64'd1) << 64'd3) | ((((A >> 64'd5)&64'd1)&64'd1) << 64'd4) | ((dout_49&64'd1) << 64'd5) | ((dout_54&64'd1) << 64'd6) | ((dout_59&64'd1) << 64'd7) | ((dout_64&64'd1) << 64'd8) | ((dout_69&64'd1) << 64'd9) | ((dout_74&64'd1) << 64'd10) | ((dout_79&64'd1) << 64'd11) | ((dout_82&64'd1) << 64'd12);
endmodule

/***
* This code is a part of EvoApproxLib library (ehw.fit.vutbr.cz/approxlib) distributed under The MIT License.
* When used, please cite the following article(s): V. Mrazek, L. Sekanina, Z. Vasicek "Libraries of Approximate Circuits: Automated Design and Application in CNN Accelerators" IEEE Journal on Emerging and Selected Topics in Circuits and Systems, Vol 10, No 4, 2020 
* This file contains a circuit from a sub-set of pareto optimal circuits with respect to the pwr and mae parameters
***/
// MAE% = 0.073 %
// MAE = 3.0 
// WCE% = 0.22 %
// WCE = 9.0 
// WCRE% = 700.00 %
// EP% = 90.62 %
// MRE% = 1.15 %
// MSE = 14 
// PDK45_PWR = 0.038 mW
// PDK45_AREA = 79.3 um2
// PDK45_DELAY = 0.68 ns

module approx_5TE(input wire [11:0] A, B, output wire [12:0] Y);
wire [63:0] dout_26;
wire [63:0] dout_41;
wire [63:0] dout_42;
wire [63:0] dout_43;
wire [63:0] dout_44;
wire [63:0] dout_45;
wire [63:0] dout_46;
wire [63:0] dout_47;
wire [63:0] dout_48;
wire [63:0] dout_49;
wire [63:0] dout_50;
wire [63:0] dout_51;
wire [63:0] dout_52;
wire [63:0] dout_53;
wire [63:0] dout_54;
wire [63:0] dout_55;
wire [63:0] dout_56;
wire [63:0] dout_57;
wire [63:0] dout_58;
wire [63:0] dout_59;
wire [63:0] dout_60;
wire [63:0] dout_61;
wire [63:0] dout_62;
wire [63:0] dout_63;
wire [63:0] dout_64;
wire [63:0] dout_65;
wire [63:0] dout_66;
wire [63:0] dout_67;
wire [63:0] dout_68;
wire [63:0] dout_69;
wire [63:0] dout_70;
wire [63:0] dout_71;
wire [63:0] dout_72;
wire [63:0] dout_73;
wire [63:0] dout_74;
wire [63:0] dout_75;
wire [63:0] dout_76;
wire [63:0] dout_77;
wire [63:0] dout_78;
wire [63:0] dout_79;
wire [63:0] dout_80;
wire [63:0] dout_81;
wire [63:0] dout_82;
assign dout_26 = ((B >> 64'd3)&64'd1)^64'd18446744073709551615;
assign dout_41 = ((A >> 64'd4)&64'd1)^((B >> 64'd4)&64'd1);
assign dout_42 = ((A >> 64'd4)&64'd1)&((B >> 64'd4)&64'd1);
assign dout_43 = dout_41&((B >> 64'd3)&64'd1);
assign dout_44 = dout_41^((B >> 64'd3)&64'd1);
assign dout_45 = dout_42|dout_43;
assign dout_46 = ((A >> 64'd5)&64'd1)^((B >> 64'd5)&64'd1);
assign dout_47 = ((A >> 64'd5)&64'd1)&((B >> 64'd5)&64'd1);
assign dout_48 = dout_46&dout_45;
assign dout_49 = dout_46^dout_45;
assign dout_50 = dout_47|dout_48;
assign dout_51 = ((A >> 64'd6)&64'd1)^((B >> 64'd6)&64'd1);
assign dout_52 = ((A >> 64'd6)&64'd1)&((B >> 64'd6)&64'd1);
assign dout_53 = dout_51&dout_50;
assign dout_54 = dout_51^dout_50;
assign dout_55 = dout_52|dout_53;
assign dout_56 = ((A >> 64'd7)&64'd1)^((B >> 64'd7)&64'd1);
assign dout_57 = ((A >> 64'd7)&64'd1)&((B >> 64'd7)&64'd1);
assign dout_58 = dout_56&dout_55;
assign dout_59 = dout_56^dout_55;
assign dout_60 = dout_57|dout_58;
assign dout_61 = ((A >> 64'd8)&64'd1)^((B >> 64'd8)&64'd1);
assign dout_62 = ((A >> 64'd8)&64'd1)&((B >> 64'd8)&64'd1);
assign dout_63 = dout_61&dout_60;
assign dout_64 = dout_61^dout_60;
assign dout_65 = dout_62|dout_63;
assign dout_66 = ((A >> 64'd9)&64'd1)^((B >> 64'd9)&64'd1);
assign dout_67 = ((A >> 64'd9)&64'd1)&((B >> 64'd9)&64'd1);
assign dout_68 = dout_66&dout_65;
assign dout_69 = dout_66^dout_65;
assign dout_70 = dout_67|dout_68;
assign dout_71 = ((A >> 64'd10)&64'd1)^((B >> 64'd10)&64'd1);
assign dout_72 = ((A >> 64'd10)&64'd1)&((B >> 64'd10)&64'd1);
assign dout_73 = dout_71&dout_70;
assign dout_74 = dout_71^dout_70;
assign dout_75 = dout_72|dout_73;
assign dout_76 = ((A >> 64'd11)&64'd1)^((B >> 64'd11)&64'd1);
assign dout_77 = ((A >> 64'd11)&64'd1)&((B >> 64'd11)&64'd1);
assign dout_78 = dout_76&dout_75;
assign dout_79 = dout_76^dout_75;
assign dout_80 = dout_77|dout_78;
assign dout_81 = ((A >> 64'd11)&64'd1)^((B >> 64'd11)&64'd1);
assign dout_82 = dout_81^dout_80;
assign Y = ((dout_77&64'd1) << 64'd0) | ((((B >> 64'd1)&64'd1)&64'd1) << 64'd1) | ((((A >> 64'd3)&64'd1)&64'd1) << 64'd2) | ((dout_26&64'd1) << 64'd3) | ((dout_44&64'd1) << 64'd4) | ((dout_49&64'd1) << 64'd5) | ((dout_54&64'd1) << 64'd6) | ((dout_59&64'd1) << 64'd7) | ((dout_64&64'd1) << 64'd8) | ((dout_69&64'd1) << 64'd9) | ((dout_74&64'd1) << 64'd10) | ((dout_79&64'd1) << 64'd11) | ((dout_82&64'd1) << 64'd12);
endmodule

/***
* This code is a part of EvoApproxLib library (ehw.fit.vutbr.cz/approxlib) distributed under The MIT License.
* When used, please cite the following article(s): V. Mrazek, L. Sekanina, Z. Vasicek "Libraries of Approximate Circuits: Automated Design and Application in CNN Accelerators" IEEE Journal on Emerging and Selected Topics in Circuits and Systems, Vol 10, No 4, 2020 
* This file contains a circuit from a sub-set of pareto optimal circuits with respect to the pwr and mae parameters
***/
// MAE% = 0.81 %
// MAE = 33 
// WCE% = 2.10 %
// WCE = 86 
// WCRE% = 6400.00 %
// EP% = 99.22 %
// MRE% = 12.63 %
// MSE = 1513 
// PDK45_PWR = 0.023 mW
// PDK45_AREA = 48.8 um2
// PDK45_DELAY = 0.43 ns

module approx_5Z0(input wire [11:0] A, B, output wire [12:0] Y);
wire [63:0] dout_38;
wire [63:0] dout_39;
wire [63:0] dout_40;
wire [63:0] dout_41;
wire [63:0] dout_42;
wire [63:0] dout_43;
wire [63:0] dout_44;
wire [63:0] dout_45;
wire [63:0] dout_46;
wire [63:0] dout_47;
wire [63:0] dout_48;
wire [63:0] dout_58;
wire [63:0] dout_59;
wire [63:0] dout_60;
wire [63:0] dout_61;
wire [63:0] dout_62;
wire [63:0] dout_63;
wire [63:0] dout_74;
wire [63:0] dout_75;
wire [63:0] dout_76;
wire [63:0] dout_77;
wire [63:0] dout_78;
wire [63:0] dout_79;
wire [63:0] dout_88;
wire [63:0] dout_89;
wire [63:0] dout_90;
wire [63:0] dout_91;
wire [63:0] dout_92;
wire [63:0] dout_93;
wire [63:0] dout_94;
wire [63:0] dout_95;
wire [63:0] dout_96;
wire [63:0] dout_97;
wire [63:0] dout_98;
wire [63:0] dout_99;
wire [63:0] dout_100;
wire [63:0] dout_107;
wire [63:0] dout_108;
wire [63:0] dout_109;
wire [63:0] dout_110;
wire [63:0] dout_111;
wire [63:0] dout_112;
assign dout_38 = ((A >> 64'd7)&64'd1)&((B >> 64'd7)&64'd1);
assign dout_39 = ((A >> 64'd7)&64'd1)^((B >> 64'd7)&64'd1);
assign dout_40 = ((A >> 64'd8)&64'd1)&((B >> 64'd8)&64'd1);
assign dout_41 = ((A >> 64'd8)&64'd1)^((B >> 64'd8)&64'd1);
assign dout_42 = ((A >> 64'd9)&64'd1)&((B >> 64'd9)&64'd1);
assign dout_43 = ((A >> 64'd9)&64'd1)^((B >> 64'd9)&64'd1);
assign dout_44 = ((A >> 64'd10)&64'd1)&((B >> 64'd10)&64'd1);
assign dout_45 = ((A >> 64'd10)&64'd1)^((B >> 64'd10)&64'd1);
assign dout_46 = ((A >> 64'd11)&64'd1)&((B >> 64'd11)&64'd1);
assign dout_47 = ((A >> 64'd11)&64'd1)^((B >> 64'd11)&64'd1);
assign dout_48 = ((A >> 64'd11)&64'd1)^((B >> 64'd11)&64'd1);
assign dout_58 = dout_41&dout_38;
assign dout_59 = dout_41&dout_39;
assign dout_60 = dout_40|dout_58;
assign dout_61 = dout_45&dout_42;
assign dout_62 = dout_45&dout_43;
assign dout_63 = dout_44|dout_61;
assign dout_74 = dout_43&dout_60;
assign dout_75 = dout_43&dout_59;
assign dout_76 = dout_42|dout_74;
assign dout_77 = dout_62&dout_60;
assign dout_78 = dout_62&dout_59;
assign dout_79 = dout_63|dout_77;
assign dout_88 = dout_47&dout_79;
assign dout_89 = dout_47&dout_78;
assign dout_90 = dout_46|dout_88;
assign dout_91 = dout_39&((B >> 64'd6)&64'd1);
assign dout_92 = dout_38|dout_91;
assign dout_93 = dout_59&((B >> 64'd6)&64'd1);
assign dout_94 = dout_60|dout_93;
assign dout_95 = dout_75&((B >> 64'd6)&64'd1);
assign dout_96 = dout_76|dout_95;
assign dout_97 = dout_78&((B >> 64'd6)&64'd1);
assign dout_98 = dout_79|dout_97;
assign dout_99 = dout_89&((B >> 64'd6)&64'd1);
assign dout_100 = dout_90|dout_99;
assign dout_107 = dout_39^((B >> 64'd6)&64'd1);
assign dout_108 = dout_41^dout_92;
assign dout_109 = dout_43^dout_94;
assign dout_110 = dout_45^dout_96;
assign dout_111 = dout_47^dout_98;
assign dout_112 = dout_48^dout_100;
assign Y = ((dout_111&64'd1) << 64'd0) | ((64'd0&64'd1) << 64'd1) | ((64'd0&64'd1) << 64'd2) | ((((A >> 64'd4)&64'd1)&64'd1) << 64'd3) | ((64'd0&64'd1) << 64'd4) | ((((A >> 64'd5)&64'd1)&64'd1) << 64'd5) | ((((A >> 64'd6)&64'd1)&64'd1) << 64'd6) | ((dout_107&64'd1) << 64'd7) | ((dout_108&64'd1) << 64'd8) | ((dout_109&64'd1) << 64'd9) | ((dout_110&64'd1) << 64'd10) | ((dout_111&64'd1) << 64'd11) | ((dout_112&64'd1) << 64'd12);
endmodule
