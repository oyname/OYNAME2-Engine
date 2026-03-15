#pragma once

#include "GDXECSRenderer.h"
#include "GDXEventQueue.h"
#include "Components.h"
#include "SubmeshData.h"

#include <vector>
#include <cstdint>

class SnakeGame
{
public:
    explicit SnakeGame(GDXECSRenderer& renderer);

    void Init();
    void Update(float deltaTime);
    void OnEvent(const Event& e);

private:
    struct Cell
    {
        int x = 0;
        int z = 0;

        bool operator==(const Cell& other) const
        {
            return x == other.x && z == other.z;
        }
    };

    enum class Direction
    {
        Up,
        Down,
        Left,
        Right
    };

    void CreateStaticScene();
    void CreateBoard();
    void CreateCamera();
    void CreateLight();

    void ResetGame();
    void SyncSnakeVisuals();
    void MoveSnakeStep();
    void SpawnFood();
    bool IsInsideBoard(const Cell& c) const;
    bool IsCellOccupiedBySnake(const Cell& c) const;
    bool IsOpposite(Direction a, Direction b) const;
    Cell StepFrom(const Cell& c, Direction dir) const;
    Float3 CellToWorld(const Cell& c, float y = 0.0f) const;
    uint32_t NextRandom();

private:
    GDXECSRenderer& m_renderer;

    EntityID m_camera = NULL_ENTITY;
    EntityID m_light = NULL_ENTITY;
    EntityID m_board = NULL_ENTITY;
    EntityID m_foodEntity = NULL_ENTITY;

    MeshHandle m_cubeMesh;

    MaterialHandle m_boardMat;
    MaterialHandle m_wallMat;
    MaterialHandle m_headMat;
    MaterialHandle m_bodyMat;
    MaterialHandle m_foodMat;
    MaterialHandle m_gameOverMat;

    std::vector<EntityID> m_wallEntities;
    std::vector<EntityID> m_bodyEntities;
    std::vector<Cell>     m_snakeCells;

    Cell m_foodCell{ 0, 0 };

    Direction m_currentDir = Direction::Right;
    Direction m_nextDir    = Direction::Right;

    float m_stepTimer = 0.0f;
    float m_stepInterval = 0.18f;
    bool  m_started = false;
    bool  m_gameOver = false;
    bool  m_pendingGrow = false;

    uint32_t m_rngState = 0x12345678u;

    static constexpr int   BOARD_W = 18;
    static constexpr int   BOARD_H = 18;
    static constexpr float CELL_SIZE = 1.10f;
    static constexpr float SEGMENT_SCALE = 0.92f;
};
