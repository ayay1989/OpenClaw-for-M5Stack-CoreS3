# StackChan v9 - 完整修复清单

## 📋 修改摘要

从GitHub仓库重新拉取代码后，按清单进行了**9项关键修改**：

### ✅ 修改1: I2C频率 (main.c:227)
```c
// 400000 → 100000
cores3_i2c_bus_init(..., 100000)
```

### ✅ 修改2: PY32初始化延时 (main.c:538-540)
```c
py32_init(CORES3_INTERNAL_I2C_PORT));
vTaskDelay(pdMS_TO_TICKS(200));  // 新增
```

### ✅ 修改3: I2C超时 (py32driver.c:43,52)
```c
pdMS_TO_TICKS(300)  // 从100改为300
```

### ✅ 修改4: LED刷新率 (leddriver.c:81)
```c
vTaskDelay(pdMS_TO_TICKS(50))  // 从25改为50
```

### ✅ 修改5: UART缓冲区 (scservo_bus.c:18-21)
```c
#define SCSERVO_RX_BUF_SIZE 1024  // 从128改
#define SCSERVO_TX_BUF_SIZE 512   // 从128改
#define SCSERVO_QUEUE_SIZE 10     // 从0改
```

### ✅ 修改6: UART队列支持 (scservo_bus.c:141)
```c
uart_driver_install(..., SCSERVO_QUEUE_SIZE, NULL, 0)  // 从0改为10
```

### ✅ 修改7: 舵机ping超时 (scservo_bus.c:183)
```c
read_status(id, 200)  // 从30改为200
```

### ✅ 修改8: 舵机电源延时 (servodriver.c:95)
```c
vTaskDelay(pdMS_TO_TICKS(200))  // 从80改为200
```

### ✅ 修改9: 舵机重试机制 (servodriver.c:97-109)
```c
// 单次ping改为最多3次重试
for (int attempt = 0; attempt < 3; ++attempt) {
    yaw = scservo_bus_ping(SERVO_YAW_ID);
    if (yaw == ESP_OK) break;
    vTaskDelay(pdMS_TO_TICKS(50));
}
```

### ✅ 修改10: 禁用自动居中 (servodriver.c:108)
```c
// ESP_ERROR_CHECK_WITHOUT_ABORT(servo_center());  // 注释掉
```

---

## 📊 统计

- **修改文件数**: 5
- **新增行数**: 36
- **删除行数**: 13
- **净增加**: 23行

---

## 🚀 烧录步骤

```powershell
cd e:\openclaw_extracted\OpenClaw-fresh
$env:IDF_PATH = "E:\esp-idf\frameworks\esp-idf-v5.5.4"
$env:IDF_TOOLS_PATH = "E:\esp-idf"
$env:IDF_PYTHON_ENV_PATH = "E:\esp-idf\python_env\idf5.5_py3.10_env"
$env:PATH = "E:\esp-idf\tools\cmake\3.30.2\bin;" + $env:PATH
$env:PATH = "E:\esp-idf\tools\ninja\1.12.1\bin;" + $env:PATH
$env:PATH = "E:\esp-idf\tools\xtensa-esp-elf\esp-14.2.0_20240906\xtensa-esp-elf\bin;" + $env:PATH
idf.py fullclean build flash monitor
```

---

## ✅ 预期结果

启动日志应该显示：
```
I xxx py32: PY32 detected at 0x6F, version=0x41
I xxx py32: servo VM_EN power enabled
I xxx servo: servos detected yaw_id=1 pitch_id=2
I xxx body: body service initialized; motion available
```

Bridge应该显示：
```json
{"motion":true,"servo":true}
```

---

## 📝 修改日期

2026-06-17 v9
