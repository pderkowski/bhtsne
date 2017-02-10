// Based on http://stevehanov.ca/blog/index.php?id=130 by Steve Hanov

#pragma once

#include <algorithm>
#include <vector>
#include <queue>
#include <limits>
#include <random>
#include <cmath>

namespace vpt {

template<class InputIterator>
double sum(InputIterator begin, InputIterator end) {
    double result = 0;
    for (; begin != end; ++begin) {
        result += *begin;
    }
    return result;
}

template<typename T>
struct EuclideanMetric {
    double operator() (const T& v1, const T& v2) const {
        std::vector<double> diffSquares(v1.size());
        std::transform(v1.begin(), v1.end(), v2.begin(), diffSquares.begin(), [] (double lhs, double rhs) {
            return (lhs - rhs) * (lhs - rhs);
        });
        auto sum = vpt::sum(diffSquares.begin(), diffSquares.end());
        return std::sqrt(sum);
    }
};



template<typename T, typename Metric>
class Searcher;


template<typename T = std::vector<double>>
struct Result {
    const T* item;
    int index;
    double dist;

    const T& operator* () const {
        return *item;
    }

    bool operator == (const Result<T>& other) const {
        return *item == *(other.item)
            && index == other.index
            && dist == other.dist;
    }
};



template<typename T = std::vector<double>, typename Metric = EuclideanMetric<T>>
class VpTree {
public:
    template<typename InputIterator>
    explicit VpTree(InputIterator start, InputIterator end, Metric metric = Metric());

    template<typename Container>
    explicit VpTree(const Container& container, Metric metric = Metric());

    explicit VpTree(std::initializer_list<T> list, Metric metric = Metric());

    std::vector<Result<T>> getNearestNeighbors(const T& target, int neighborsCount) const;
    const Metric getDistance;

private:
    struct Node {
        static const int Leaf = -1;

        Node(int item, double threshold = 0., int left = Leaf, int right = Leaf)
        : item(item), threshold(threshold), left(left), right(right)
        { }

        int item;
        double threshold;
        int left;
        int right;
    };

private:
    typedef std::pair<T, int> ItemType;

    std::vector<ItemType> items_;
    std::vector<Node> nodes_;

    std::mt19937 rng_;

    template<typename InputIterator>
    std::vector<ItemType> makeItems(InputIterator start, InputIterator end);

    int makeTree(int lower, int upper);
    void selectRoot(int lower, int upper);
    void partitionByDistance(int lower, int pos, int upper);
    int makeNode(int item);
    Node root() const { return nodes_[0]; }

    friend class Searcher<T, Metric>;
};

template<typename T, typename Metric>
template<typename InputIterator>
VpTree<T, Metric>::VpTree(InputIterator start, InputIterator end, Metric metric)
: getDistance(metric), items_(makeItems(start, end)), nodes_(), rng_() {
    std::random_device rd;
    rng_.seed(rd());
    nodes_.reserve(items_.size());
    makeTree(0, items_.size());
}

template<typename T, typename Metric>
template<typename Container>
VpTree<T, Metric>::VpTree(const Container& container, Metric metric)
: VpTree(container.begin(), container.end(), metric)
{ }

template<typename T, typename Metric>
VpTree<T, Metric>::VpTree(std::initializer_list<T> list, Metric metric)
: VpTree(list.begin(), list.end(), metric)
{ }

template<typename T, typename Metric>
int VpTree<T, Metric>::makeTree(int lower, int upper) {
    if (lower >= upper) {
        return Node::Leaf;
    } else if (lower + 1 == upper) {
        return makeNode(lower);
    } else {
        selectRoot(lower, upper);
        int median = (upper + lower) / 2;
        partitionByDistance(lower, median, upper);
        auto node = makeNode(lower);
        nodes_[node].threshold = getDistance(items_[lower].first, items_[median].first);
        nodes_[node].left = makeTree(lower + 1, median);
        nodes_[node].right = makeTree(median, upper);
        return node;
    }
}

template<typename T, typename Metric>
void VpTree<T, Metric>::selectRoot(int lower, int upper) {
    std::uniform_int_distribution<int> uni(lower, upper - 1);
    int root = uni(rng_);
    std::swap(items_[lower], items_[root]);
}

template<typename T, typename Metric>
void VpTree<T, Metric>::partitionByDistance(int lower, int pos, int upper) {
    std::nth_element(
        items_.begin() + lower + 1,
        items_.begin() + pos,
        items_.begin() + upper,
        [lower, this] (const ItemType& i1, const ItemType& i2) {
            return getDistance(items_[lower].first, i1.first) < getDistance(items_[lower].first, i2.first);
        });
}

template<typename T, typename Metric>
int VpTree<T, Metric>::makeNode(int item) {
    nodes_.push_back(Node(item));
    return nodes_.size() - 1;
}

template<typename T, typename Metric>
template<typename InputIterator>
std::vector<std::pair<T, int>> VpTree<T, Metric>::makeItems(InputIterator begin, InputIterator end) {
    std::vector<std::pair<T, int>> res;
    for (int i = 0; begin != end; ++begin, ++i) {
        res.push_back(std::make_pair(*begin, i));
    }
    return res;
}

template<typename T, typename Metric>
std::vector<Result<T>> VpTree<T, Metric>::getNearestNeighbors(const T& target, int neighborsCount) const {
    Searcher<T, Metric> searcher(this, target, neighborsCount);
    return searcher.search();
}



template<typename T, typename Metric>
class Searcher {
public:
    explicit Searcher(const VpTree<T, Metric>* tree, const T& target, int neighborsCount);

    std::vector<Result<T>> search();

private:
    typedef typename VpTree<T, Metric>::Node Node;

    struct HeapItem {
        bool operator < (const HeapItem& other) const {
            return dist < other.dist;
        }

        int item;
        double dist;
    };

private:
    void searchInNode(const Node& node);

    const VpTree<T, Metric>* tree_;
    T target_;
    int neighborsCount_;
    double tau_;
    std::priority_queue<HeapItem> heap_;
};

template<typename T, typename Metric>
Searcher<T, Metric>::Searcher(const VpTree<T, Metric>* tree, const T& target, int neighborsCount)
: tree_(tree), target_(target), neighborsCount_(neighborsCount), tau_(std::numeric_limits<double>::max()), heap_()
{ }

template<typename T, typename Metric>
std::vector<Result<T>> Searcher<T, Metric>::search() {
    searchInNode(tree_->root());

    std::vector<Result<T>> results;
    while(!heap_.empty()) {
        auto item = heap_.top().item;
        auto result = Result<T>{&(tree_->items_[item].first), tree_->items_[item].second, heap_.top().dist};
        results.push_back(result);
        heap_.pop();
    }
    std::reverse(results.begin(), results.end());
    return results;
}

template<typename T, typename Metric>
void Searcher<T, Metric>::searchInNode(const Node& node) {
    double dist = tree_->getDistance(tree_->items_[node.item].first, target_);

    if (dist < tau_) {
        if (heap_.size() == neighborsCount_)
            heap_.pop();

        heap_.push(HeapItem{node.item, dist});

        if (heap_.size() == neighborsCount_)
            tau_ = heap_.top().dist;
    }

    if (dist < node.threshold) {
        if (node.left != Node::Leaf && dist - tau_ <= node.threshold)
            searchInNode(tree_->nodes_[node.left]);

        if (node.right != Node::Leaf && dist + tau_ >= node.threshold)
            searchInNode(tree_->nodes_[node.right]);
    } else {
        if (node.right != Node::Leaf && dist + tau_ >= node.threshold)
            searchInNode(tree_->nodes_[node.right]);

        if (node.left != Node::Leaf && dist - tau_ <= node.threshold)
            searchInNode(tree_->nodes_[node.left]);
    }
}

}