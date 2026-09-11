pragma Singleton
import QtQuick
QtObject {
    property bool simulationRunning: true
    property bool missionPaused: false
    property bool missionUploaded: false
    property bool missionRunning: false
    property bool missionBusy: false
    property bool sensorServiceConnected: true
    property int lastSeek: -1
    function seekReplay(ms) { lastSeek = ms }
    property QtObject gimbalInspection: QtObject {
        property bool active: false
        property bool picking: false
        property bool historical: false
        property bool hasTarget: true
        property bool canRecord: true
        property bool automatic: true
        property string name: "测试建筑目标"
        property string guidance: "较高视角参考：距离 23.6 m（要求 3—40 m）；相对选点仰角 78.5°（要求 15°至35°）。视角过高：拉开水平距离或降低高度；请自行避障。"
        property string status: "已锁定；请手动移动到所选观察方向。此处为较长状态说明，必须完整换行，不遮挡右侧控件。"
        property int completedCount: 1
        property int savedCount: 2
        property var observation: ({available:true,x:10,y:20,deltaEast:14,deltaNorth:-5,deltaUp:3,alignmentError:1.2})
        property int selectedView: 0
        property int guideView: 0
        property var views: [
            {label:"正面",complete:true,saved:true,source:"",elapsedMs:1200},
            {label:"左斜视",complete:false,saved:true,source:""},
            {label:"右斜视",complete:false,source:""},
            {label:"较高视角",complete:false,source:""}
        ]
        function start() { active = true }
        function pause() { active = false }
        function retake() {}
        function capture() {}
    }
}
