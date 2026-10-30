# Environment V2 — оригинальные bitmap-материалы

Созданы процедурно для OnlineTower, 30 октября 2026 года. Внешние изображения и сторонние ассеты не использовались. Источник — `generate_textures.py`; детерминированный синтез NumPy, фиксированные seeds. Выполнен bundled Python Blender 4.3, версия NumPy 1.24.3.

Семь наборов Rock, Sandstone, Sand, Terrazzo, Wood, Metal, Shrub. Каждый содержит BaseColor.png (sRGB), Normal.png (linear DirectX tangent-space) и Roughness.png (linear), 1024×1024, RGB PNG. Все шумы и cellular-поля периодические, нормали вычислены через wrap-around производные. Набор не содержит displacement, opacity, AO или готовые модели растительности.

Preview.png: сверху вниз наборы в указанном порядке; слева BaseColor, посередине Normal, справа Roughness. Preview просмотрен визуально: различимы трещины/слои камня, песчаные полосы, крошка терраццо, древесные волокна, металл и плотный растительный узор. Это стилизованные поверхности, не фотоскан.

Импорт и UE-материалы создаёт `OnlineTower 5.8/Scripts/import_environment_textures.py`. Все три карты проецируются по мировым координатам с triplanar blending; TileSize задаётся в сантиметрах. Для normal используется linear RGB без BC5, поскольку Custom HLSL декодирует XYZ и переводит три проекции в world-space. У материалов выключен tangent_space_normal и включён UsedWithInstancedStaticMeshes. Проверка фактической компиляции шейдеров и отображения в UE выполняется координатором отдельно.

Старые 13 процедурных материалов перед исправлением ISM-флага копируются в `/Game/Core/Materials/Backups/EnvironmentV2`. Существующие V2-текстуры не перезаписываются; повторный запуск чинит usage-флаг существующих V2-материалов, но не пересоздаёт их графы. Для изменения уже импортированных исходников нужен отдельный явный reimport.
