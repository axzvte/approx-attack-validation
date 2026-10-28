# Extend offline per-image ROI experiments to Conv, Sobel and DCT

User authorizes adapting code and running the same experiment on the remaining three applications.
Reuse validated offline ROI and staged images from the completed Sharpening run read-only.
Each application independently selects its own fixed hardware on images1–10, freezes its own seed bank, and calibrates intervals on images31–40. Keep original arithmetic and search code unchanged. Preserve all old outputs.

- [x] RED/GREEN: default batch includes Sobel; new strict ROI tests cover independent Conv/Sobel/DCT child reports, external ROI directory, native mask checks and full image exports.
- [x] Add a portable background launcher using a completed stage-1 run; fresh output required; explicit three-application list, full images, status/log/summary in VS Code project.
- [ ] Run existing and new tests, review changes, publish code, save isolated source snapshot under VS Code project and execute real three-application batch.

Verification: synthetic tests use small budgets and threshold0 for interface checks only. Real experiments retain original defaults including global PSNR30, max80 train evaluations, context64/128 and workers4. Numeric eligibility remains distinct from visual success.

Full CTest verification: 65/65 passed, 0 failed, in 93.08 seconds. New launcher and batch tests were observed failing before implementation; native Conv/Sobel/DCT tests passed with no changes to search algorithms. Actual real-data experiment status is reported separately from synthetic verification.
