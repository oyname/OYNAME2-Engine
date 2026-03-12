#include "SnakeGame.h"

#include <algorithm>
#include <cmath>

SnakeGame::SnakeGame(GDXECSRenderer& renderer)
    : m_renderer(renderer)
{
}

void SnakeGame::Init()
{
    Registry& reg = m_renderer.GetRegistry();

    MeshAssetResource cubeAsset;
    cubeAsset.debugName = "SnakeCube";
    cubeAsset.AddSubmesh(BuiltinMeshes::Cube());
    m_cubeMesh = m_renderer.UploadMesh(std::move(cubeAsset));

    m_boardMat    = m_renderer.CreateMaterial(MaterialResource::FlatColor(0.10f, 0.10f, 0.12f));
    m_wallMat     = m_renderer.CreateMaterial(MaterialResource::FlatColor(0.24f, 0.24f, 0.28f));
    m_headMat     = m_renderer.CreateMaterial(MaterialResource::FlatColor(0.22f, 0.92f, 0.30f));
    m_bodyMat     = m_renderer.CreateMaterial(MaterialResource::FlatColor(0.10f, 0.62f, 0.18f));
    m_foodMat     = m_renderer.CreateMaterial(MaterialResource::FlatColor(0.96f, 0.16f, 0.16f));
    m_gameOverMat = m_renderer.CreateMaterial(MaterialResource::FlatColor(0.90f, 0.20f, 0.12f));

    CreateStaticScene();

    m_foodEntity = reg.CreateEntity();
    reg.Add<TagComponent>(m_foodEntity, "Food");
    reg.Add<TransformComponent>(m_foodEntity);
    reg.Add<WorldTransformComponent>(m_foodEntity);
    reg.Add<MeshRefComponent>(m_foodEntity, m_cubeMesh, 0u);
    reg.Add<MaterialRefComponent>(m_foodEntity, m_foodMat);
    reg.Add<VisibilityComponent>(m_foodEntity);

    m_renderer.SetSceneAmbient(0.35f, 0.35f, 0.38f);
    m_renderer.SetClearColor(0.025f, 0.025f, 0.035f);

    ResetGame();
}

void SnakeGame::CreateStaticScene()
{
    CreateBoard();
    CreateCamera();
    CreateLight();
}

void SnakeGame::CreateBoard()
{
    Registry& reg = m_renderer.GetRegistry();

    m_board = reg.CreateEntity();
    reg.Add<TagComponent>(m_board, "Board");
    {
        TransformComponent tc;
        tc.localPosition = { 0.0f, -0.65f, 0.0f };
        tc.localScale = { BOARD_W * CELL_SIZE * 0.5f + 1.0f, 0.25f, BOARD_H * CELL_SIZE * 0.5f + 1.0f };
        reg.Add<TransformComponent>(m_board, tc);
    }
    reg.Add<WorldTransformComponent>(m_board);
    reg.Add<MeshRefComponent>(m_board, m_cubeMesh, 0u);
    reg.Add<MaterialRefComponent>(m_board, m_boardMat);
    reg.Add<VisibilityComponent>(m_board);

    for (int z = -1; z <= BOARD_H; ++z)
    {
        for (int x = -1; x <= BOARD_W; ++x)
        {
            const bool isBorder = (x == -1 || z == -1 || x == BOARD_W || z == BOARD_H);
            if (!isBorder)
                continue;

            EntityID wall = reg.CreateEntity();
            m_wallEntities.push_back(wall);

            reg.Add<TagComponent>(wall, "Wall");
            {
                TransformComponent tc;
                tc.localPosition = CellToWorld({ x, z }, 0.10f);
                tc.localScale = { 1.0f, 1.0f, 1.0f };
                reg.Add<TransformComponent>(wall, tc);
            }
            reg.Add<WorldTransformComponent>(wall);
            reg.Add<MeshRefComponent>(wall, m_cubeMesh, 0u);
            reg.Add<MaterialRefComponent>(wall, m_wallMat);
            reg.Add<VisibilityComponent>(wall);
        }
    }
}

void SnakeGame::CreateCamera()
{
    Registry& reg = m_renderer.GetRegistry();

    m_camera = reg.CreateEntity();
    reg.Add<TagComponent>(m_camera, "SnakeCamera");
    {
        TransformComponent tc;
        tc.localPosition = { 0.0f, 20.0f, 0.0f };
        tc.SetEulerDeg(90.0f, 0.0f, 0.0f);
        reg.Add<TransformComponent>(m_camera, tc);
    }
    reg.Add<WorldTransformComponent>(m_camera);

    CameraComponent cam;
    cam.isOrtho = true;
    cam.orthoWidth = BOARD_W * CELL_SIZE + 3.0f;
    cam.orthoHeight = BOARD_H * CELL_SIZE + 3.0f;
    cam.nearPlane = 0.1f;
    cam.farPlane = 100.0f;
    cam.aspectRatio = 16.0f / 9.0f;
    reg.Add<CameraComponent>(m_camera, cam);
    reg.Add<ActiveCameraTag>(m_camera);
}

void SnakeGame::CreateLight()
{
    Registry& reg = m_renderer.GetRegistry();

    m_light = reg.CreateEntity();
    reg.Add<TagComponent>(m_light, "SnakeSun");

    LightComponent lc;
    lc.kind = LightKind::Directional;
    lc.diffuseColor = { 1.0f, 1.0f, 1.0f, 1.0f };
    lc.intensity = 1.2f;
    lc.castShadows = false;
    reg.Add<LightComponent>(m_light, lc);

    TransformComponent tc;
    tc.SetEulerDeg(-65.0f, 35.0f, 0.0f);
    reg.Add<TransformComponent>(m_light, tc);
    reg.Add<WorldTransformComponent>(m_light);
}

void SnakeGame::ResetGame()
{
    Registry& reg = m_renderer.GetRegistry();

    for (EntityID id : m_bodyEntities)
    {
        reg.DestroyEntity(id);
    }
    m_bodyEntities.clear();
    m_snakeCells.clear();

    const int midX = BOARD_W / 2;
    const int midZ = BOARD_H / 2;

    m_snakeCells.push_back({ midX,     midZ });
    m_snakeCells.push_back({ midX - 1, midZ });
    m_snakeCells.push_back({ midX - 2, midZ });

    m_currentDir = Direction::Right;
    m_nextDir = Direction::Right;
    m_stepTimer = 0.0f;
    m_started = false;
    m_gameOver = false;
    m_pendingGrow = false;

    SpawnFood();
    SyncSnakeVisuals();
}

void SnakeGame::Update(float deltaTime)
{
    if (m_gameOver)
        return;

    if (!m_started)
        return;

    m_stepTimer += deltaTime;

    while (m_stepTimer >= m_stepInterval)
    {
        m_stepTimer -= m_stepInterval;
        MoveSnakeStep();

        if (m_gameOver)
            break;
    }
}

void SnakeGame::OnEvent(const Event& e)
{
    std::visit([this](auto&& ev)
    {
        using T = std::decay_t<decltype(ev)>;

        if constexpr (std::is_same_v<T, KeyPressedEvent>)
        {
            if (ev.repeat)
                return;

            Direction requested = m_nextDir;
            bool validDirection = true;

            switch (ev.key)
            {
            case Key::Up:
            case Key::W: requested = Direction::Up;    break;
            case Key::Down:
            case Key::S: requested = Direction::Down;  break;
            case Key::Left:
            case Key::A: requested = Direction::Left;  break;
            case Key::Right:
            case Key::D: requested = Direction::Right; break;
            case Key::Space:
                if (m_gameOver)
                    ResetGame();
                else
                    m_started = !m_started;
                return;
            default:
                validDirection = false;
                break;
            }

            if (!validDirection)
                return;

            if (!IsOpposite(m_currentDir, requested) || m_snakeCells.size() <= 1)
            {
                m_nextDir = requested;
                m_started = true;
            }
        }
    }, e);
}

void SnakeGame::MoveSnakeStep()
{
    if (m_snakeCells.empty())
        return;

    m_currentDir = m_nextDir;
    const Cell newHead = StepFrom(m_snakeCells.front(), m_currentDir);

    if (!IsInsideBoard(newHead))
    {
        m_gameOver = true;
        SyncSnakeVisuals();
        return;
    }

    const bool eatsFood = (newHead == m_foodCell);

    Cell tailBeforeMove{};
    if (!m_snakeCells.empty())
        tailBeforeMove = m_snakeCells.back();

    m_snakeCells.insert(m_snakeCells.begin(), newHead);

    const size_t collisionCheckCount = eatsFood ? m_snakeCells.size() : (m_snakeCells.size() - 1);
    for (size_t i = 1; i < collisionCheckCount; ++i)
    {
        if (m_snakeCells[i] == newHead)
        {
            m_gameOver = true;
            SyncSnakeVisuals();
            return;
        }
    }

    if (!eatsFood)
    {
        m_snakeCells.pop_back();
    }
    else
    {
        SpawnFood();
        if (m_stepInterval > 0.08f)
            m_stepInterval -= 0.005f;
    }

    (void)tailBeforeMove;
    SyncSnakeVisuals();
}

void SnakeGame::SpawnFood()
{
    const int cellCount = BOARD_W * BOARD_H;
    if ((int)m_snakeCells.size() >= cellCount)
    {
        m_foodCell = { -1000, -1000 };
        if (auto* vis = m_renderer.GetRegistry().Get<VisibilityComponent>(m_foodEntity))
            vis->visible = false;
        return;
    }

    Registry& reg = m_renderer.GetRegistry();
    if (auto* vis = reg.Get<VisibilityComponent>(m_foodEntity))
        vis->visible = true;

    for (int attempts = 0; attempts < 512; ++attempts)
    {
        const int x = static_cast<int>(NextRandom() % static_cast<uint32_t>(BOARD_W));
        const int z = static_cast<int>(NextRandom() % static_cast<uint32_t>(BOARD_H));
        const Cell candidate{ x, z };
        if (!IsCellOccupiedBySnake(candidate))
        {
            m_foodCell = candidate;
            if (auto* tc = reg.Get<TransformComponent>(m_foodEntity))
            {
                tc->localPosition = CellToWorld(m_foodCell, 0.05f);
                tc->localScale = { 0.72f, 0.72f, 0.72f };
                tc->dirty = true;
            }
            return;
        }
    }
}

void SnakeGame::SyncSnakeVisuals()
{
    Registry& reg = m_renderer.GetRegistry();

    while (m_bodyEntities.size() < m_snakeCells.size())
    {
        const EntityID e = reg.CreateEntity();
        m_bodyEntities.push_back(e);

        reg.Add<TagComponent>(e, "SnakeSegment");
        reg.Add<TransformComponent>(e);
        reg.Add<WorldTransformComponent>(e);
        reg.Add<MeshRefComponent>(e, m_cubeMesh, 0u);
        reg.Add<MaterialRefComponent>(e, m_bodyMat);
        reg.Add<VisibilityComponent>(e);
    }

    while (m_bodyEntities.size() > m_snakeCells.size())
    {
        reg.DestroyEntity(m_bodyEntities.back());
        m_bodyEntities.pop_back();
    }

    for (size_t i = 0; i < m_bodyEntities.size(); ++i)
    {
        const EntityID id = m_bodyEntities[i];

        if (auto* tc = reg.Get<TransformComponent>(id))
        {
            tc->localPosition = CellToWorld(m_snakeCells[i], 0.05f);
            tc->localScale = { SEGMENT_SCALE, SEGMENT_SCALE, SEGMENT_SCALE };
            tc->dirty = true;
        }

        if (auto* mat = reg.Get<MaterialRefComponent>(id))
        {
            if (i == 0)
                mat->material = m_gameOver ? m_gameOverMat : m_headMat;
            else
                mat->material = m_bodyMat;
        }
    }
}

bool SnakeGame::IsInsideBoard(const Cell& c) const
{
    return c.x >= 0 && c.x < BOARD_W && c.z >= 0 && c.z < BOARD_H;
}

bool SnakeGame::IsCellOccupiedBySnake(const Cell& c) const
{
    return std::find(m_snakeCells.begin(), m_snakeCells.end(), c) != m_snakeCells.end();
}

bool SnakeGame::IsOpposite(Direction a, Direction b) const
{
    return (a == Direction::Up    && b == Direction::Down)
        || (a == Direction::Down  && b == Direction::Up)
        || (a == Direction::Left  && b == Direction::Right)
        || (a == Direction::Right && b == Direction::Left);
}

SnakeGame::Cell SnakeGame::StepFrom(const Cell& c, Direction dir) const
{
    Cell out = c;
    switch (dir)
    {
    case Direction::Up:    out.z += 1; break;
    case Direction::Down:  out.z -= 1; break;
    case Direction::Left:  out.x -= 1; break;
    case Direction::Right: out.x += 1; break;
    }
    return out;
}

DirectX::XMFLOAT3 SnakeGame::CellToWorld(const Cell& c, float y) const
{
    const float originX = -((BOARD_W - 1) * CELL_SIZE) * 0.5f;
    const float originZ = -((BOARD_H - 1) * CELL_SIZE) * 0.5f;
    return {
        originX + static_cast<float>(c.x) * CELL_SIZE,
        y,
        originZ + static_cast<float>(c.z) * CELL_SIZE
    };
}

uint32_t SnakeGame::NextRandom()
{
    m_rngState = m_rngState * 1664525u + 1013904223u;
    return m_rngState;
}
