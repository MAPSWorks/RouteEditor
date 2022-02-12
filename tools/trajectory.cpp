#include    "trajectory.h"

#include    <QFile>
#include    <QDir>
#include    <QTextStream>
#include    <execution>
#include    <vsg/io/read.h>
#include    "topology.h"
#include    "sceneobjects.h"
#include    "LambdaVisitor.h"
#include    <vsg/nodes/VertexIndexDraw.h>
#include    <vsg/nodes/StateGroup.h>
#include    <vsg/io/ObjectCache.h>


//------------------------------------------------------------------------------
//
//------------------------------------------------------------------------------

namespace route
{
    SplineTrajectory::SplineTrajectory(std::string name,
                                       vsg::ref_ptr<RailConnector> bwdPoint,
                                       vsg::ref_ptr<RailConnector> fwdPoint,
                                       vsg::ref_ptr<vsg::Builder> builder,
                                       tinyobj::attrib_t rail,
                                       tinyobj::attrib_t fill,
                                       vsg::ref_ptr<vsg::Data> rtexture,
                                       vsg::ref_ptr<vsg::Data> ftexture,
                                       vsg::ref_ptr<vsg::Node> sleeper, double distance, double gaudge)
      : vsg::Inherit<Trajectory, SplineTrajectory>(name)
      , _builder(builder)
      , _track(vsg::MatrixTransform::create())
      , _sleeper(sleeper)
      , _sleepersDistance(distance)
      , _gaudge(gaudge)
      , _fwdPoint(fwdPoint)
      , _bwdPoint(bwdPoint)
    {
        auto it = rail.vertices.begin();
        auto end = rail.vertices.end() - (rail.vertices.size()/2);
        while (it < end)
        {
            _rail.vertices.push_back(vsg::vec3(*it, *(it + 1), *(it + 2)));
            it += 3;
        }

        it = rail.texcoords.begin();
        end = rail.texcoords.end() - (rail.texcoords.size()/2);
        while (it < end)
            _rail.uv.push_back(vsg::vec2(*it++, *it++));


        it = fill.vertices.begin();
        end = fill.vertices.end() - (fill.vertices.size()/2);
        while (it < end)
        {
            _fill.vertices.push_back(vsg::vec3(*it, *(it + 1), *(it + 2)));
            it += 3;
        }

        it = fill.texcoords.begin();
        end = fill.texcoords.end() - (fill.texcoords.size()/2);
        while (it < end)
            _fill.uv.push_back(vsg::vec2(*it++, *it++));


        fwdPoint->setBwd(this);
        bwdPoint->setFwd(this);

        _rail.texture = rtexture;
        _fill.texture = ftexture;

        subgraphRequiresLocalFrustum = false;

        SplineTrajectory::recalculate();
    }

    SplineTrajectory::SplineTrajectory()
      : vsg::Inherit<Trajectory, SplineTrajectory>()
    {
    }
    SplineTrajectory::~SplineTrajectory()
    {
        _bwdPoint->setNull(this);
        _fwdPoint->setNull(this);
    }
    //------------------------------------------------------------------------------
    //
    //------------------------------------------------------------------------------

    void SplineTrajectory::read(vsg::Input& input)
    {
        Group::read(input);

        input.read("sleepersDistance", _sleepersDistance);
        input.read("gaudge", _gaudge);
        input.read("autoPositioned", _autoPositioned);
        input.read("points", _points);
        input.read("railsVerts", _rail.vertices);
        input.read("railsUV", _rail.uv);
        input.read("railTexture", _rail.texture);
        input.read("fillVerts", _fill.vertices);
        input.read("fillUV", _fill.uv);
        input.read("fillTexture", _fill.texture);
        input.read("sleeper", _sleeper);

        input.read("fwdPoint", _fwdPoint);
        input.read("bwdPoint", _bwdPoint);

        input.read("track", _track);

        updateSpline();
    }

    void SplineTrajectory::write(vsg::Output& output) const
    {
        Group::write(output);

        output.write("sleepersDistance", _sleepersDistance);
        output.write("gaudge", _gaudge);
        output.write("autoPositioned", _autoPositioned);
        output.write("points", _points);
        output.write("railsVerts", _rail.vertices);
        output.write("railsUV", _rail.uv);
        output.write("railTexture", _rail.texture);
        output.write("fillVerts", _fill.vertices);
        output.write("fillUV", _fill.uv);
        output.write("fillTexture", _fill.texture);
        output.write("sleeper", _sleeper);

        output.write("fwdPoint", _fwdPoint);
        output.write("bwdPoint", _bwdPoint);

        output.write("track", _track);
    }

    vsg::dvec3 SplineTrajectory::getCoordinate(double x) const
    {
        double T = ArcLength::solveLength(*_railSpline, 0.0, x);
        return _railSpline->getPosition(T);
    }

    double SplineTrajectory::invert(const vsg::dvec3 vec) const
    {
        SplineInverter<vsg::dvec3, double> inverter(*_railSpline);
        auto T = inverter.findClosestT(vec);
        return _railSpline->arcLength(0, T);
    }

    vsg::dmat4 SplineTrajectory::getMatrixAt(double x) const
    {
        double T = ArcLength::solveLength(*_railSpline, 0.0, x);
        auto pt = _railSpline->getTangent(T);
        return InterpolatedPTM(std::move(pt)).calculated;
    }

    void SplineTrajectory::updateSpline()
    {
        std::vector<vsg::dvec3> points;
        std::vector<vsg::dvec3> tangents;

        auto front = _fwdPoint->getPosition();

        points.push_back(front);
        tangents.push_back(_fwdPoint->getTangent());

        std::transform(_points.begin(), _points.end(), std::back_insert_iterator(points),
                       [](const vsg::ref_ptr<RailPoint> sp)
        {
            return sp->getPosition();
        });
        std::transform(_points.begin(), _points.end(), std::back_insert_iterator(tangents),
                       [](const vsg::ref_ptr<RailPoint> sp)
        {
            return sp->getTangent();
        });

        points.push_back(_bwdPoint->getPosition());
        tangents.push_back(_bwdPoint->getTangent());

        _railSpline.reset(new InterpolationSpline(points, tangents));
    }

    void SplineTrajectory::recalculate()
    {
        updateSpline();

        auto n = std::ceil(_railSpline->totalLength() / _sleepersDistance);

        auto partitionBoundaries = ArcLength::partitionN(*_railSpline, static_cast<size_t>(n));

        auto front = _fwdPoint->getPosition();
        _track->matrix = vsg::translate(front);

        std::vector<InterpolatedPTM> derivatives(partitionBoundaries.size());
        std::transform(std::execution::par_unseq, partitionBoundaries.begin(), partitionBoundaries.end(), derivatives.begin(),
                       [railSpline=_railSpline, front](double T)
        {
            return std::move(InterpolatedPTM(railSpline->getTangent(T), front));
        });

        _track->children.clear();
        size_t index = 0;
        for (auto &ptcm : derivatives)
        {
            auto transform = vsg::MatrixTransform::create(ptcm.calculated);
            transform->addChild(_sleeper);
            _track->addChild(transform);
            ptcm.index = index;
            index++;
        }

        //auto last = std::unique(std::execution::par, derivatives.begin(), derivatives.end()); //vectorization is not supported
        //derivatives.erase(last, derivatives.end());

        assignRails(derivatives);

        updateAttached();
    }

    vsg::ref_ptr<vsg::VertexIndexDraw> SplineTrajectory::createGeometry(const vsg::vec3 &offset,
                                                                        const std::vector<InterpolatedPTM> &derivatives,
                                                                        ModelData geometry) const
    {
        std::vector<std::pair<std::vector<vsg::vec3>, size_t>> vertexGroups(derivatives.size());

        std::transform(std::execution::par_unseq, derivatives.begin(), derivatives.end(), vertexGroups.begin(),
        [geometry, offset](const InterpolatedPTM &ptcm)
        {
            auto fmat = static_cast<vsg::mat4>(ptcm.calculated);
            std::vector<vsg::vec3> out;

            for(const auto &vec : geometry.vertices)
                out.push_back(fmat * (vec + offset));



            return std::make_pair(std::move(out), ptcm.index);
        });

        auto vsize = vertexGroups.size() * geometry.vertices.size();
        auto vertArray = vsg::vec3Array::create(vsize);
        auto texArray = vsg::vec2Array::create(vsize);
        auto vertIt = vertArray->begin();
        auto texIt = texArray->begin();

        for(const auto &vertexGroup : vertexGroups)
        {
            auto uv = geometry.uv.begin();
            auto end = geometry.uv.end();
            for(const auto &vertex : vertexGroup.first)
            {
                Q_ASSERT(vertIt != vertArray->end());
                Q_ASSERT(uv < end);
                *vertIt = vertex;
                *texIt = vsg::vec2(vertexGroup.second, uv->y);
                texIt++;
                uv++;
                vertIt++;
            }
        }

        std::vector<uint16_t> indices;
        const auto next = static_cast<uint16_t>(geometry.vertices.size());
        uint16_t max = vsize - next - 1;

        for (uint16_t ind = 0; ind < max; ++ind)
        {
            if((ind + 1) % next == 0)
                continue;
            indices.push_back(ind);
            indices.push_back(ind + next);
            indices.push_back(ind + next + 1);
            indices.push_back(ind + next + 1);
            indices.push_back(ind + 1);
            indices.push_back(ind);
        }

        auto colorArray = vsg::vec4Array::create(vsize);
        std::fill(colorArray->begin(), colorArray->end(), vsg::vec4(1.0f, 0.0f, 1.0f, 1.0f));

        auto normalArray = vsg::vec3Array::create(vsize);
        std::fill(normalArray->begin(), normalArray->end(), vsg::vec3(1.0f, 0.0f, 0.0f));

        auto ind = vsg::ushortArray::create(indices.size());
        std::copy(indices.begin(), indices.end(), ind->begin());

        auto vid = vsg::VertexIndexDraw::create();

        vid->assignArrays(vsg::DataList{vertArray, normalArray, texArray});

        vid->assignIndices(ind);
        vid->indexCount = static_cast<uint32_t>(ind->size());
        vid->instanceCount = 1;

        return vid;
    }

    void SplineTrajectory::assignRails(const std::vector<InterpolatedPTM> &derivatives)
    {
        auto left = createGeometry(vsg::vec3(_gaudge / 2.0, 0.0, 0.0), derivatives, _rail);
        auto right = createGeometry(vsg::vec3(-_gaudge / 2.0, 0.0, 0.0), derivatives, _rail);
        auto fill = createGeometry(vsg::vec3(), derivatives, _fill);

        vsg::StateInfo si;
        si.image = _rail.texture;
        //si.lighting = false;

        auto rstateGroup = _builder->createStateGroup(si);

        rstateGroup->addChild(left);
        rstateGroup->addChild(right);

        si.image = _fill.texture;

        auto fstateGroup = _builder->createStateGroup(si);

        fstateGroup->addChild(fill);

        _track->addChild(rstateGroup);
        _track->addChild(fstateGroup);

        _builder->compileTraversal->compile(_track);
    }

    void SplineTrajectory::updateAttached()
    {
        auto computeTransform = [this](vsg::MatrixTransform& transform)
        {
            double coord = 0.0;
            if(transform.getValue(META_PROPERTY, coord))
                transform.matrix = getMatrixAt(coord);
        };
        LambdaVisitor<decltype (computeTransform), vsg::MatrixTransform> ct(computeTransform);
        Trajectory::accept(ct);
    }

    size_t SplineTrajectory::add(vsg::ref_ptr<RailPoint> rp)
    {
        SplineInverter<vsg::dvec3, double> inverter(*_railSpline);
        double t = inverter.findClosestT(rp->getPosition());
        auto index = static_cast<size_t>(std::floor(t)) + 1;
        auto it = _points.begin() + index;
        Q_ASSERT(it <= _points.end());
        _points.insert(it, rp);
        rp->trajectory = this;

        recalculate();

        return index;
    }

    void SplineTrajectory::remove(size_t index)
    {
        auto it = _points.begin() + index;
        Q_ASSERT(it < _points.end());
        _points.erase(it);
    }

    void SplineTrajectory::remove(vsg::ref_ptr<RailPoint> rp)
    {
        auto it = std::find(_points.begin(), _points.end(), rp);
        _points.erase(it);
    }

    Junction::Junction(std::string name,
                       vsg::ref_ptr<RailConnector> bwdPoint,
                       vsg::ref_ptr<RailConnector> fwdPoint,
                       vsg::ref_ptr<RailConnector> fwd2Point,
                       vsg::ref_ptr<vsg::AnimationPath> strait,
                       vsg::ref_ptr<vsg::AnimationPath> side,
                       vsg::ref_ptr<vsg::AnimationPath> switcherPath,
                       vsg::ref_ptr<vsg::Node> rails,
                       vsg::ref_ptr<vsg::MatrixTransform> switcher)
      : vsg::Inherit<Trajectory, Junction>(name)
      , _strait(strait)
      , _side(side)
      , _fwdPoint(fwdPoint)
      , _fwd2Point(fwd2Point)
      , _bwdPoint(bwdPoint)
    {
        Q_ASSERT(_side->locations.size() > 1);
        Q_ASSERT(_strait->locations.size() > 1);


        fwdPoint->setBwd(this);
        fwd2Point->setBwd(this);
        bwdPoint->setFwd(this);
    }

    Junction::Junction()
      : vsg::Inherit<Trajectory, Junction>()
    {
    }
    Junction::~Junction()
    {
        _bwdPoint->setFwd(nullptr);
        _fwdPoint->setBwd(nullptr);
    }
    //------------------------------------------------------------------------------
    //
    //------------------------------------------------------------------------------

    void Junction::read(vsg::Input& input)
    {
        Group::read(input);

        input.read("straitTrajecory", _strait);
        input.read("sideTrajecory", _side);
        input.read("switcher", _switcher);

        input.read("fwdPoint", _fwdPoint);
        input.read("fwd2Point", _fwd2Point);
        input.read("bwdPoint", _bwdPoint);
    }

    void Junction::write(vsg::Output& output) const
    {
        Group::write(output);

        output.write("straitTrajecory", _strait);
        output.write("sideTrajecory", _side);
        output.write("switcher", _switcher);

        output.write("fwdPoint", _fwdPoint);
        output.write("fwd2Point", _fwd2Point);
        output.write("bwdPoint", _bwdPoint);
    }

    void Junction::setPosition(const vsg::dvec3 &pos)
    {
        _position = pos;
        auto mat = transform(vsg::dmat4());
        _fwdPoint->localToWorld = mat;
        _fwd2Point->localToWorld = mat;
        _bwdPoint->localToWorld = mat;
    }

    void Junction::setRotation(const vsg::dquat &rot)
    {
        _quat = rot;
        auto mat = transform(vsg::dmat4());
        _fwdPoint->localToWorld = mat;
        _fwd2Point->localToWorld = mat;
        _bwdPoint->localToWorld = mat;
    }

    vsg::dvec3 Junction::getCoordinate(double x) const
    {
        auto &path = _state ? _side : _strait;
        return transform(vsg::dmat4()) * path->computeLocation(x).position;
    }

    double Junction::invert(const vsg::dvec3 vec) const
    {
        return 0.0;
    }

    vsg::dmat4 Junction::getMatrixAt(double x) const
    {
        auto &path = _state ? _side : _strait;
        return transform(path->computeMatrix(x));
    }

    SceneTrajectory::SceneTrajectory()
        : vsg::Inherit<vsg::Group, SceneTrajectory>()
    {
    }
    SceneTrajectory::SceneTrajectory(Trajectory *traj)
        : SceneTrajectory()
    {
        children.emplace_back(traj);
    }

    SceneTrajectory::~SceneTrajectory() {}

    void SceneTrajectory::read(vsg::Input& input)
    {
        Node::read(input);

        std::string name;
        input.read("trajName", name);

        addChild(input.options->objectCache->get(TOPOLOGY_KEY).cast<Topology>()->trajectories.at(name));

    }

    void SceneTrajectory::write(vsg::Output& output) const
    {
        Node::write(output);

        auto trajectory = children.front();
        Q_ASSERT(trajectory);

        std::string name;
        trajectory->getValue(META_NAME, name);
        output.write("trajName", name);

    }
}
