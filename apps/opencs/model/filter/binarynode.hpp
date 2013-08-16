#ifndef CSM_FILTER_BINARYNODE_H
#define CSM_FILTER_BINARYNODE_H

#include <boost/shared_ptr.hpp>

#include "node.hpp"

namespace CSMFilter
{
    class BinaryNode : public Node
    {
            boost::shared_ptr<Node> mLeft;
            boost::shared_ptr<Node> mRight;

        public:

            BinaryNode (boost::shared_ptr<Node> left, boost::shared_ptr<Node> right);

            const Node& getLeft() const;

            Node& getLeft();

            const Node& getRight() const;

            Node& getRight();

            virtual std::vector<std::string> getReferencedFilters() const;
            ///< Return a list of filters that are used by this node (and must be passed as
            /// otherFilters when calling test).

            virtual std::vector<int> getReferencedColumns() const;
            ///< Return a list of the IDs of the columns referenced by this node. The column mapping
            /// passed into test as columns must contain all columns listed here.

            virtual bool isSimple() const;
            ///< \return Can this filter be displayed in simple mode.

            virtual bool hasUserValue() const;
    };
}

#endif
