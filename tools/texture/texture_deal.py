import cv2
import numpy as np
import os
import glob

def extract_and_crop(image_path, output_dir, min_area=10, save_preview=True):
    """
    连通域分析 → tight crop → 输出每个独立实体。
    每个实体裁到像素边缘，零多余空间。
    """
    image = cv2.imread(image_path, cv2.IMREAD_UNCHANGED)

    if image is None:
        print(f"  [警告] 无法读取图像: {image_path}")
        return None

    if len(image.shape) < 3 or image.shape[2] != 4:
        print(f"  [警告] 图像不包含 Alpha 通道，跳过: {image_path}")
        return None

    base_name = os.path.splitext(os.path.basename(image_path))[0]
    entity_dir = os.path.join(output_dir, base_name)
    os.makedirs(entity_dir, exist_ok=True)

    # Alpha 二值化 → 连通域
    alpha = image[:, :, 3]
    _, mask = cv2.threshold(alpha, 0, 255, cv2.THRESH_BINARY)
    num_labels, labels, stats, _ = cv2.connectedComponentsWithStats(mask, connectivity=8)

    results = []
    preview = image.copy() if save_preview else None

    for i in range(1, num_labels):
        x, y = stats[i, cv2.CC_STAT_LEFT], stats[i, cv2.CC_STAT_TOP]
        w, h = stats[i, cv2.CC_STAT_WIDTH], stats[i, cv2.CC_STAT_HEIGHT]
        area = stats[i, cv2.CC_STAT_AREA]

        if area < min_area:
            continue

        # Tight crop
        cropped = image[y:y+h, x:x+w].copy()
        entity_path = os.path.join(entity_dir, f"{base_name}_{len(results)}.png")
        cv2.imwrite(entity_path, cropped)

        results.append({'id': len(results), 'x': int(x), 'y': int(y), 'w': int(w), 'h': int(h)})

        if preview is not None:
            cv2.rectangle(preview, (x, y), (x + w, y + h), (0, 255, 0, 255), 2)

    # 预览图
    if preview is not None:
        cv2.imwrite(os.path.join(entity_dir, f"{base_name}_preview.png"), preview)

    # Lua 坐标表（原图中的位置，引擎侧可用）
    with open(os.path.join(entity_dir, f"{base_name}_data.lua"), 'w', encoding='utf-8') as f:
        f.write("return {\n")
        for t in results:
            f.write(f"    [{t['id']}] = {{ x = {t['x']}, y = {t['y']}, w = {t['w']}, h = {t['h']} }},\n")
        f.write("}\n")

    print(f"  -> {base_name}: {len(results)} 个实体已裁剪输出至 {entity_dir}")
    return results


def batch_process(input_dir, output_dir, min_area=20):
    """批量处理文件夹中所有 PNG"""
    os.makedirs(output_dir, exist_ok=True)
    files = glob.glob(os.path.join(input_dir, "*.png"))

    if not files:
        print(f"在 '{input_dir}' 中没有找到任何 PNG 图片。")
        return

    print(f"开始处理，共 {len(files)} 张图...\n" + "=" * 40)
    for f in files:
        print(f"正在分析: {os.path.basename(f)}")
        extract_and_crop(f, output_dir, min_area=min_area)
    print("=" * 40 + "\n处理完毕！")


if __name__ == "__main__":
    base_dir = os.path.dirname(os.path.abspath(__file__))
    batch_process(
        input_dir=os.path.join(base_dir, "input_sprites"),
        output_dir=os.path.join(base_dir, "output_results"),
        min_area=20
    )