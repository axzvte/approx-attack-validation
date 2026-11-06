import argparse
import json
from pathlib import Path
import tempfile
import unittest

import numpy as np
from PIL import Image
from scipy.ndimage import convolve1d
from skimage.color import rgb2lab

import color_saliency as c
import union_frequency_rectangles as u


class ColorSaliencyTests(unittest.TestCase):
    def test_ft_matches_binomial_lab_definition(self):
        rgb=np.zeros((9,11,3),np.uint8)
        rgb[3:6,4:7]=[200,40,20]
        lab=rgb2lab(rgb/255.)
        kernel=np.array([1,4,6,4,1])/16
        smooth=convolve1d(convolve1d(lab,kernel,axis=0,mode='reflect'),kernel,axis=1,mode='reflect')
        expected=np.sqrt(((smooth-lab.mean(axis=(0,1)))**2).sum(axis=2))
        np.testing.assert_allclose(c.compute_ft(rgb),expected,rtol=1e-6,atol=1e-6)

    def test_constant_images_and_region_outputs(self):
        rgb=np.full((40,60,3),80,np.uint8)
        self.assertLess(float(c.compute_ft(rgb).max()),1e-10)
        self.assertLess(float(c.compute_rc(rgb).max()),1e-10)
        rgb[12:28,20:40]=[220,20,20]
        for compute in (c.compute_ft,c.compute_rc):
            response=compute(rgb)
            self.assertEqual(response.shape,rgb.shape[:2])
            self.assertTrue(np.isfinite(response).all())
            self.assertGreater(response[20,30],response[2,2])

    def test_region_contrast_against_explicit_histogram_sum(self):
        hist=np.array([[.8,.2,0],[0,.4,.6],[0,0,1]])
        colors=np.array([[0,0,0],[20,0,0],[50,0,0]],float)
        centers=np.array([[.1,.2],[.3,.7],[.8,.9]])
        weights=np.array([.2,.3,.5])
        expected=[]
        for i in range(3):
            score=0
            for j in range(3):
                if i==j: continue
                distance=sum(hist[i,a]*hist[j,b]*np.linalg.norm(colors[a]-colors[b])
                             for a in range(3) for b in range(3))
                score+=np.exp(-np.linalg.norm(centers[i]-centers[j])/.4)*weights[j]*distance
            expected.append(score)
        np.testing.assert_allclose(c.regional_contrast(hist,colors,centers,weights),expected,atol=1e-12)

    def test_color_quantization_preserves_all_pixel_assignments(self):
        rgb=np.zeros((10,10,3),np.uint8)
        rgb[:2]=[230,10,10]
        rgb[-1,-1]=[0,255,0]
        indices,colors=c.quantize_colors(rgb)
        self.assertEqual(indices.shape,(10,10))
        self.assertEqual(len(colors),2)
        self.assertLess(int(indices.max()),2)

    def test_rc_ft_end_to_end_and_preserves_inputs(self):
        with tempfile.TemporaryDirectory() as directory:
            root=Path(directory)
            (root/'datasets/imgs/pascal').mkdir(parents=True)
            (root/'pred_masks').mkdir()
            rgb=np.full((40,60,3),40,np.uint8)
            rgb[10:30,20:40]=[220,20,20]
            mask=np.zeros((40,60),np.uint8);mask[12:28,22:38]=255
            image_path=root/'datasets/imgs/pascal/1.jpg'
            Image.fromarray(rgb).save(image_path)
            Image.fromarray(mask).save(root/'pred_masks/1_mask.png')
            before=image_path.read_bytes()
            for feature in ['rc','ft']:
                args=argparse.Namespace(data=root,mask_dir='pred_masks',image_dir='datasets/imgs/pascal',
                      pc_dir=None,output=root/feature,start=1,count=1,grid=32,preview_count=1,
                      thresholds=[50,72,95],feature=feature)
                u.run(args)
                protocol=json.loads((args.output/'protocol.json').read_text())
                self.assertEqual(protocol['status'],'completed')
                self.assertEqual(protocol['feature'],feature)
                self.assertTrue((args.output/f'{feature}_responses/1.npz').is_file())
                count=np.load(args.output/'frequencies/union_count.npy')
                a=np.load(args.output/'frequencies/u2net_count.npy')
                added=np.load(args.output/'frequencies/added_count.npy')
                np.testing.assert_array_equal(count,a+added)
                self.assertTrue((args.output/'thresholds/union/72_rectangle_mask.png').is_file())
                self.assertTrue((args.output/f'per_image_masks/{feature}/1.png').is_file())
            self.assertEqual(before,image_path.read_bytes())


if __name__=='__main__': unittest.main()
