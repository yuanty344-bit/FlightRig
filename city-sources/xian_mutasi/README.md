# 西安木塔寺城市公园场景

`xian_mutasi` 是 FlightBench 的首个通用城市生成器正式场景，覆盖木塔寺生态遗址公园及周边商业、住宅和道路。

- 中心坐标：34.210120° N，108.889750° E
- 范围：中心向四周各 500 米，约 1 km × 1 km
- OSM 数据时间：2026-08-21T21:29:20Z
- 建筑：59 栋
- 道路：66 段
- 绿地、水面等区域：7 个
- 兴趣点：21 个
- 建筑避障屋顶三角形：197 个
- 局部坐标：X 向东、Y 向北、Z 向上

场景原点位于公园开阔区域，检查时未落入任何建筑轮廓。没有 OSM 高度的建筑使用确定性规则估算：优先采用 `height`，其次使用 `building:levels × 3.2 m`，最后根据占地面积使用 9、12 或 15 米，不使用随机高度。

`source/osm.json` 保存原始查询结果，`source/overpass.query` 保存可复现的查询；`xian_mutasi.scene.json` 是统一场景数据，Gazebo、Qt 三维白模和二维任务地图均由它生成。

数据来源：© OpenStreetMap contributors，Open Database License (ODbL)。
