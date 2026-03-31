import { Container, Typography, Grid, Card, CardContent, Button } from '@mui/material';

const writings = [
  {
    title: "On Wet T-Shirt Contests",
    description: "What explains wet t-shirt contests and how do similar events relate to the concept of cybernetics, wireheading? How do people voluntarily perform in such contests?",
    link: "/WetT-Shirt.pdf",
    date: "March 31 2026"
  },
  {
    title: "Knowing the Ultimate Form of Reality",
    description: "What is the ultimate form of reality, and what are the obstacles to understanding it? Why we may live in a quantum multiverse according to the Schrodinger's equation. Being thankful for the miracle of our existence.",
    link: "/TrueNature.pdf",
    date: "March 29 2026"
  },
  {
    title: "Religiosity and Transhumanism",
    description: "What are the parallels between transhumanism and religion? Is transhumanism, and more broadly, science, also a religion?",
    link: "/ReligiosityH+.pdf",
    date: "March 24 2026"
  },
  {
    title: 'Note on Efficacy of ML and Beautiful Algos',
    description: "A short note on how ML and gradient descent can exploit low-dimensional manifolds to solve very difficult problems, in many cases, converge to very clean representations (i.e. very few to no heuristics) with high accuracy and speed. Does this imply that, e.x. object recognition, CFD can have clean, beautiful, non-neural algorithms as well? What does this imply about the existence of low-dimensional structure in almost everything? Are the learned algorithms even comprehensible, or are they going to remain as indecipherable matrices of floating point numbers?",
    link: '/BeautifulAlgo.pdf',
    date: 'March 3 2026'
  },
  {
    title: 'On Technological Progress',
    description: "Why does recent technological progress seem awful, despite incredible developments to the contrary? What is the nature of technological progress? How does disruption reset our outlook on the future and possibly give us renewed hope?",
    link: '/TechProgress.pdf',
    date: 'February 24 2026'
  },
  {
    title: 'The Value of Voluntary Effort',
    description: "Explores why voluntary effort for creativity, careful thinking and problem solving are still so important, even if something can do them better. Individual understanding, learning and exercise of the imagination (creativity) are the primary reason to do things.",
    link: '/Voluntary.pdf',
    date: 'February 2026'
  },
  {
    title: 'Extrinsic vs. Intrinsic Rewards',
    description: "Some thoughts about extrinsic vs. intrinsic rewards & the difference between them as a stable means to guide one's life.",
    link: '/Rewards.pdf',
    date: 'February 2026'
  },
  {
    title: 'Post-Automation Life',
    description: "How does one maintain their humanity and happiness in a variety of crazy futures?",
    link: '/PostAutomation.pdf',
    date: 'October 2025'
  },
  {
    title: 'Balcony Reflection',
    description: "A description of a view from my balcony in my college apartment.",
    link: '/Balcony.pdf',
    date: 'May 2024'
  },
  {
    title: 'Untitled Sci-Fi',
    description: "Tried to write a science fiction story, failed.",
    link: '/SciFi.pdf',
    date: '2024'
  },
  {
    title: 'Nostalgia Essay',
    description: "Essay I wrote in HS about nostalgia. Not a very good essay.",
    link: '/Nostalgia.pdf',
    date: 'October 2019'
  },
];

export default function Writing() {
  return (
    <Container maxWidth="lg" sx={{ mt: 15, mb: 8 }}>
      <Typography variant="h2" gutterBottom sx={{ fontWeight: 'bold' }}>
        Writing
      </Typography>
      <Typography variant="h5" sx={{ color: 'text.secondary', mb: 6 }}>
        Just some thoughts; don't take seriously.
      </Typography>
      
      <Grid container spacing={4}>
        {writings.map((post, index) => (
          <Grid size={{xs: 12, md: 6}} key={index}>
            <Card 
              sx={{ 
                height: '100%', 
                display: 'flex', 
                flexDirection: 'column',
                bgcolor: 'background.paper',
                borderRadius: 2,
                transition: 'transform 0.2s',
                '&:hover': {
                  transform: 'translateY(-4px)',
                  boxShadow: (theme) => theme.shadows[4]
                }
              }}
            >
              <CardContent sx={{ flexGrow: 1, p: 4}}>
                <Typography variant="overline" color="primary" sx={{ fontWeight: 'bold' }}>
                  {post.date}
                </Typography>
                <Typography variant="h5" component="h3" gutterBottom sx={{ fontWeight: 'bold', mt: 1 }}>
                  {post.title}
                </Typography>
                <Typography variant="body1" color="text.secondary" sx={{ mb: 3, lineHeight: 1.7 }}>
                  {post.description}
                </Typography>
                <Button 
                  variant="outlined" 
                  color="primary" 
                  href={post.link} 
                  target="_blank"
                  sx={{ fontWeight: 'bold', borderRadius: 1.5 }}
                >
                  Read
                </Button>
              </CardContent>
            </Card>
          </Grid>
        ))}
      </Grid>
    </Container>
  );
}
