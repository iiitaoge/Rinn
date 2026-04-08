import cv2
import numpy as np
import os
import glob
import json # 引入 json 模块用来保存坐标数据

def extract_textures_from_spritesheet(image_path, min_area=10, output_preview_path=None):
    """
    通过连通域分析找出所有独立纹理，并返回它们的最小外接矩形信息。
    """
    image = cv2.imread(image_path, cv2.IMREAD_UNCHANGED)
    
    if image is None:
        print(f"  [警告] 无法读取图像: {image_path}")
        return None
        
    if len(image.shape) < 3 or image.shape[2] != 4:
        print(f"  [警告] 图像不包含 Alpha 通道跳过: {image_path}")
        return None

    # 1. 提取 Alpha 通道并二值化
    alpha_channel = image[:, :, 3]
    _, binary_mask = cv2.threshold(alpha_channel, 0, 255, cv2.THRESH_BINARY)

    # 2. 经典的连通域分析 (核心算法)
    num_labels, labels, stats, centroids = cv2.connectedComponentsWithStats(binary_mask, connectivity=8)

    textures_info = []

    # 3. 提取 x, y, width, height
    for i in range(1, num_labels):
        x = stats[i, cv2.CC_STAT_LEFT]
        y = stats[i, cv2.CC_STAT_TOP]
        w = stats[i, cv2.CC_STAT_WIDTH]
        h = stats[i, cv2.CC_STAT_HEIGHT]
        area = stats[i, cv2.CC_STAT_AREA]

        if area >= min_area:
            textures_info.append({
                'id': i,
                'x': int(x),
                'y': int(y),
                'width': int(w),
                'height': int(h)
            })

            # 画出绿色外接矩形框用于预览
            if output_preview_path:
                cv2.rectangle(image, (x, y), (x + w, y + h), (0, 255, 0, 255), 2)

    if output_preview_path:
        cv2.imwrite(output_preview_path, image)

    return textures_info

def batch_process_directory(input_dir, output_dir, min_area=10):
    """
    批量处理文件夹，并导出坐标数据和预览图
    """
    if not os.path.exists(output_dir):
        os.makedirs(output_dir)

    search_pattern = os.path.join(input_dir, "*.png")
    image_files = glob.glob(search_pattern)

    if not image_files:
        print(f"在 '{input_dir}' 中没有找到任何 PNG 图片。")
        return

    print(f"开始处理，共 {len(image_files)} 张大图...\n" + "="*40)

    for file_path in image_files:
        file_name = os.path.basename(file_path)
        base_name = os.path.splitext(file_name)[0]
        
        print(f"正在分析: {file_name}")

        preview_path = os.path.join(output_dir, f"{base_name}_preview.png")
        
        # 获取最核心的坐标数据
        results = extract_textures_from_spritesheet(
            image_path=file_path,
            min_area=min_area,
            output_preview_path=preview_path
        )

        if results is not None:
            # ================= 生成 LUA 表 ==============
            lua_output_path = os.path.join(output_dir, f"{base_name}_data.lua")
            
            with open(lua_output_path, 'w', encoding='utf-8') as f:
                f.write("return {\n")
                for t in results:
                    f.write(f"    [{t['id']}] = {{ x = {t['x']}, y = {t['y']}, w = {t['width']}, h = {t['height']} }},\n")
                f.write("}\n")
            
            print(f"  -> 找到 {len(results)} 个纹理，坐标已保存至: {base_name}_data.lua")
            # ===============================================

    print("="*40)
    print("所有纹理集处理完毕！数据和预览图都在输出文件夹中。")

# ================= 运行入口 =================
if __name__ == "__main__":
    base_dir = os.path.dirname(os.path.abspath(__file__))
    INPUT_FOLDER = os.path.join(base_dir, "input_sprites")   
    OUTPUT_FOLDER = os.path.join(base_dir, "output_results") 
    
    batch_process_directory(
        input_dir=INPUT_FOLDER, 
        output_dir=OUTPUT_FOLDER, 
        min_area=20  # 可调节，过滤掉面积小于 20 的噪点
    )