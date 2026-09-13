# 西安工业大学未央校区

`xatu_weiyang` 是 FlightRig 内置校园场景，覆盖西安工业大学未央校区。该场景最初由 CityInspect 工具链生成，现已将可追溯的原始资料迁入 FlightRig 的统一 `city-sources` 目录。

## 数据范围

- 地址：陕西省西安市未央区学府中路 2 号
- OpenStreetMap 校园边界：`way 293805615`
- 场景原点：北纬 `34.3808166`，东经 `108.9816047`
- 场景范围：约 `761 × 1187 m`
- 建筑：47 个
- 道路与步道：147 段
- 绿地、水体和运动场等区域：93 个
- 兴趣点：25 个

建筑高度优先采用 OSM 的 `height`，其次采用 `building:levels × 3.2 m`；缺少两者时根据占地面积使用 9、12 或 15 米的确定性估算，不使用随机高度。

## 文件说明

- `source/osm.json`：原始 OpenStreetMap 查询结果，不手工修改。
- `source/overpass.query`：限定校园边界的 Overpass 查询语句。
- `xatu_weiyang.scene.json`：统一 ENU 场景数据，场景 ID 已与 FlightRig 正式世界 ID 对齐。
- `xatu_weiyang.preview.svg`：原始数据转换后的二维预览。

当前正式运行资产位于 `simulation/worlds/xatu_weiyang.sdf`、`simulation/models/xatu_weiyang/` 和 `resources/worldmaps/xatu_weiyang*`。本次迁移只补齐源文件，没有重新生成或覆盖现有模型，避免改变已验证的坐标、几何和任务地图。

## 许可

地图数据来自 OpenStreetMap，采用 ODbL。软件中展示这些数据时必须保留：

> © OpenStreetMap contributors

许可说明：https://www.openstreetmap.org/copyright
